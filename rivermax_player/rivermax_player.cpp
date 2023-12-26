/*
 * SPDX-FileCopyrightText: Copyright (c) 2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <iomanip>
#include <stdlib.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <string.h>
#include <thread>
#include <memory>
#include <condition_variable>
#include <mutex>
#include <rivermax_api.h>
#include <functional>
#include <chrono>
#include <atomic>
#include <sstream>
#include <algorithm>
#include "rt_threads.h"
#include "readerwriterqueue/readerwriterqueue.h"
#include "CLI/CLI.hpp"
// ffmpeg
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/pixdesc.h>
#include <libavutil/channel_layout.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}
#include "defs.h"

#ifndef __linux__
#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "avformat.lib")
#endif

using namespace std::chrono;
using namespace moodycamel;

#ifdef __GNUC__
# define likely(condition) __builtin_expect(static_cast<bool>(condition), 1)
# define unlikely(condition) __builtin_expect(static_cast<bool>(condition), 0)
#else
# define likely(condition) (condition)
# define unlikely(condition) (condition)
#endif

#ifdef __linux__
#include <endian.h>
#include <arpa/inet.h>
#else
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#define htobe16(x) htons(x)
#define htobe32(x) htonl(x)
#endif

#define RMAX_PLAYER_AFFINITY "RMAX_PLAYER_AFFINITY"

uint64_t rivermax_player_time_handler(void*)
{
    return (uint64_t)duration_cast<nanoseconds>((system_clock::now() + seconds{LEAP_SECONDS}).time_since_epoch()).count();
}

uint64_t rivermax_time_handler(void*)
{
    uint64_t time = 0;
    if (RMAX_OK != rmax_get_time(RMAX_CLOCK_PTP, &time)) {
        std::cout << "Failed to retrieve Rivermax time" << std::endl;
        exit(-1);
    }
    return time;
}

uint64_t rivermax_player_time_handler(void*); // used time handler forward deceleration
static uint64_t(*p_get_current_time_ns)(void *ctx) = rivermax_player_time_handler;
/*
* g_tai_to_rmax_time_conversion when set to zero no conversion is required, otherwise
* it will be set to leap seconds
*/
static uint64_t g_tai_to_rmax_time_conversion = 0;
static bool run_threads = true;

inline uint64_t get_tai_time_ns(void)
{
    return p_get_current_time_ns(nullptr);
}

/**
 * @param time reflects TAI time and must be non zero
*/
inline uint64_t align_to_rmax_time(uint64_t time)
{
    return time - g_tai_to_rmax_time_conversion;
}

struct queued_data {
    enum {
        e_qdi_ok = 0,
        e_qdi_eof
    } queued_data_info = e_qdi_ok;

    std::shared_ptr<AVFrame> frame;
    std::shared_ptr<AVPacket> packet;
};

using my_queue = ReaderWriterQueue<std::shared_ptr<queued_data>>;

bool loop = false;
bool disable_wait_for_event = false;
bool disable_synchronization = false;
uint16_t video_tro_default_modification;

/*
 * since rivermax_player generates the ancillary data and doesn't read it from a file, we need to sleep in order
 * not to flood Rivermax
 */
int const ancillary_wakeup_delta_ms = 10;

enum eMediaType_t {
    ancillary = (1 << 0),
    video = (1 << 1),
    audio = (1 << 2),
};

uint32_t stream_type = eMediaType_t::ancillary |
                       eMediaType_t::audio |
                       eMediaType_t::video;

enum affinity_index_t {
    e_video_reader_index = 0,
    e_video_scaler_index,
    e_video_sender_index,
    e_audio_reader_index,
    e_audio_encoder_index,
    e_audio_sender_index,
    // e_ancillary_reader_index,
    // e_ancillary_sender_index,
    e_num_of_affinity_index
};

const char* affinity_index_name_t[] {
    "Video reader",
    "Video scaler",
    "Video sender",
    "Audio reader",
    "Audio encoder",
    "Audio sender",
    // "Ancillary reader",
    // "Ancillary sender",
    "N/A"
};

struct rtp_header {
    uint8_t cc : 4;            // CSRC count
    uint8_t extension : 1;     // Extension bit
    uint8_t padding : 1;       // Padding bit
    uint8_t version : 2;       // Version, currently 2
    uint8_t payload_type : 7;       // Payload type
    uint8_t marker : 1;        // Marker bit
    uint16_t sequence_number;        // sequence number
    uint32_t timestamp;       //  timestamp
    uint32_t ssrc;      // synchronization source (SSRC) identifier
};

struct srd_header {
     uint16_t srd_length;  // SRD Length: 16 bits

     uint8_t srd_row_number_8_to_14_7bit: 7; // srd raw number: 15 bits
     uint8_t f: 1;  // Field identification: 1 bit
     uint8_t srd_row_number_0_to_7_8bit; // srd raw number: 15 bits

     uint8_t srd_offset_8_to_14_7bit: 7; // srd offset: 15 bits
     uint8_t c: 1;  // Field identification: 1 bit
     uint8_t srd_offset_0_to_7_8bit; // srd offset: 15 bits

     void set_srd_row_number(uint16_t srd_raw_number) {
         srd_row_number_0_to_7_8bit = (uint8_t)srd_raw_number;
         srd_row_number_8_to_14_7bit = srd_raw_number>>8;
     }

     void set_srd_offset(uint16_t srd_offset) {
         srd_offset_0_to_7_8bit = (uint8_t)srd_offset;
         srd_offset_8_to_14_7bit = srd_offset>>8;
     }
 };

struct cst_data  // calculate stream time data
{
    cst_data() = default;
    cst_data(
        uint32_t _width
        , uint32_t _height
        , double _fps
        , VIDEO_TYPE _video_type
        , AVPixelFormat _pix_format
        , int _sample_rate) :
            width(_width)
            , height(_height)
            , fps(_fps)
            , video_type(_video_type)
            , pix_format(_pix_format)
            , sample_rate(_sample_rate)
    { }
    cst_data(uint64_t _audio_ptime_usec, int _sample_rate, double _fps) :
              fps(_fps)
            , audio_ptime_usec(_audio_ptime_usec)
            , sample_rate(_sample_rate)
    { }

    uint32_t width = 0;
    uint32_t height = 0;
    double fps = 0.0;
    VIDEO_TYPE video_type = VIDEO_TYPE::NON_VIDEO;
    uint64_t audio_ptime_usec = 0;
    AVPixelFormat pix_format = AV_PIX_FMT_NONE;
    int sample_rate = 0;
};

struct CpuAffinity
{
    CpuAffinity() {
        memset(&affinity_mask, 0, sizeof(affinity_mask));
    }
    virtual ~CpuAffinity() {}

    struct rmax_cpu_set_t affinity_mask;

public:
    void set_cpu(long cpu) {
         if (cpu != CPU_NONE) {
             memset(&affinity_mask, 0, sizeof(affinity_mask));
             RMAX_CPU_SET(cpu, &affinity_mask);
         }
    }

    struct rmax_cpu_set_t *affinity_mask_get() {
         return &affinity_mask;
    }
};

//Video
struct VideoRmaxData: CpuAffinity
{
    VideoRmaxData() = default;
    VideoRmaxData(
        uint32_t _width
        , uint32_t _height
        , int64_t _duration
        , double _fps
        , VIDEO_TYPE _video_type
        , int _payload_type
        , int _sample_rate
        , AVPixelFormat _pix_format
        , std::string &_sdp_path
        , std::shared_ptr<my_queue> &_send_cb
        , std::shared_ptr<std::condition_variable> &_send_cv
        , std::shared_ptr<std::mutex> &_send_lock
        , std::shared_ptr<std::condition_variable> &_sync_cv
        , std::shared_ptr<std::mutex> &_sync_lock
        , std::shared_ptr<std::condition_variable> &_eof_cv
        , std::shared_ptr<double> &_next_frame_field_send_time_ns
        , double _timestamp_tick
        , uint16_t _use_max_payload_size
        , bool _allow_padding
        , std::shared_ptr<std::atomic<int>> &_eof_stream_counter) :
            CpuAffinity()
            , width(_width)
            , height(_height)
            , duration(_duration)
            , fps(_fps)
            , video_type(_video_type)
            , payload_type(_payload_type)
            , sample_rate(_sample_rate)
            , pix_format(_pix_format)
            , sdp_path(_sdp_path)
            , send_cb(_send_cb)
            , send_cv(_send_cv)
            , send_lock(_send_lock)
            , sync_cv(_sync_cv)
            , sync_lock(_sync_lock)
            , eof_cv(_eof_cv)
            , next_frame_field_send_time_ns(_next_frame_field_send_time_ns)
            , timestamp_tick(_timestamp_tick)
            , max_payload_size(_use_max_payload_size)
            , allow_padding(_allow_padding)
            , eof_stream_counter(_eof_stream_counter)
    { }

    //Video
    uint16_t width = 0;
    uint16_t height = 0;
    int64_t duration = 0;
    int frame_field_size = 0;
    double fps = 0.0;
    VIDEO_TYPE video_type = VIDEO_TYPE::NON_VIDEO;
    int payload_type = 0;
    int sample_rate = 0;
    AVPixelFormat pix_format = AV_PIX_FMT_NONE;
    std::string sdp_path;
    std::shared_ptr<my_queue> send_cb;
    std::shared_ptr<std::condition_variable> send_cv;
    std::shared_ptr<std::mutex> send_lock;
    std::shared_ptr<std::condition_variable> sync_cv;
    std::shared_ptr<std::mutex> sync_lock;
    std::shared_ptr<std::condition_variable> eof_cv;
    std::shared_ptr<double> next_frame_field_send_time_ns;
    double timestamp_tick = 0.0;
    uint16_t max_payload_size = 0;
    bool allow_padding = false;
    std::shared_ptr<std::atomic<int>> eof_stream_counter;
    void notify_all_cv()
    {
        eof_stream_counter->fetch_add(1);
        eof_cv->notify_one();
        send_cv->notify_all();
        sync_cv->notify_all();
    }
};

struct ScaleDataVideo
{
    ScaleDataVideo(
        VideoRmaxData &_rmax_data
        , std::shared_ptr<my_queue> &_conv_cb
        , std::shared_ptr<std::condition_variable> &_conv_cv
        , std::shared_ptr<std::mutex> &_conv_lock, long cpu) :
        rmax_data(
            _rmax_data.width
            , _rmax_data.height
            , _rmax_data.duration
            , _rmax_data.fps
            , _rmax_data.video_type
            , _rmax_data.payload_type
            , _rmax_data.sample_rate
            , _rmax_data.pix_format
            , _rmax_data.sdp_path
            , _rmax_data.send_cb
            , _rmax_data.send_cv
            , _rmax_data.send_lock
            , _rmax_data.sync_cv
            , _rmax_data.sync_lock
            , _rmax_data.eof_cv
            , _rmax_data.next_frame_field_send_time_ns
            , _rmax_data.timestamp_tick
            , _rmax_data.max_payload_size
            , _rmax_data.allow_padding
            , _rmax_data.eof_stream_counter)
        , conv_cb(_conv_cb)
        , conv_cv(_conv_cv)
        , conv_lock(_conv_lock)
        {
            rmax_data.set_cpu(cpu);
        }

    VideoRmaxData rmax_data;
    std::shared_ptr<my_queue> conv_cb;
    std::shared_ptr<std::condition_variable> conv_cv;
    std::shared_ptr<std::mutex> conv_lock;
    void notify_all_cv()
    {
        conv_cv->notify_all();
        rmax_data.send_cv->notify_all();
        rmax_data.sync_cv->notify_all();
    }

public:
    struct rmax_cpu_set_t *affinity_mask_get()
    {
        return rmax_data.affinity_mask_get();
    }
};

struct VideoReaderData: CpuAffinity
{
    VideoReaderData() = default;
    VideoReaderData(
        std::shared_ptr<AVFormatContext*> _p_format_context
        , AVCodec *_p_codec
        , AVCodecParameters *_p_codec_parameters
        , int _video_stream_index
        , std::string _file_path
        , std::shared_ptr<my_queue> &_conv_cb
        , std::shared_ptr<std::condition_variable> &_conv_cv
        , std::shared_ptr<std::mutex> &_conv_lock
        , VIDEO_TYPE _video_type
        , long cpu) :
            CpuAffinity()
            , p_format_context(_p_format_context)
            , p_codec(_p_codec)
            , p_codec_parameters(_p_codec_parameters)
            , stream_index(_video_stream_index)
            , file_path(_file_path)
            , conv_cb(_conv_cb)
            , conv_cv(_conv_cv)
            , conv_lock(_conv_lock)
            , video_type(_video_type)
    {
        set_cpu(cpu);
    }

    std::shared_ptr<AVFormatContext*> p_format_context = nullptr;
    const AVCodec *p_codec = nullptr;
    AVCodecParameters *p_codec_parameters = nullptr;
    int stream_index = -1;
    std::string file_path;
    std::shared_ptr<my_queue> conv_cb;
    std::shared_ptr<std::condition_variable> conv_cv;
    std::shared_ptr<std::mutex> conv_lock;
    const char *stream_name = "video";
    const int ffmpeg_thread_count = 5;
    VIDEO_TYPE video_type = VIDEO_TYPE::NON_VIDEO;
    void notify_all_cv()
    {
        conv_cv->notify_all();
    }
};

//Audio
struct AudioRmaxData: CpuAffinity
{
    AudioRmaxData() = default;
    AudioRmaxData(
        int64_t _bit_rate
        , int _sample_rate
        , int _channels
        , int _frame_size
        , uint64_t _channel_layout
        , uint64_t _ptime_usec
        , int _payload_type
        , std::string &_sdp_path
        , std::shared_ptr<my_queue> &_send_cb
        , std::shared_ptr<std::condition_variable> &_send_cv
        , std::shared_ptr<std::mutex> &_send_lock
        , std::shared_ptr<std::condition_variable> &_sync_cv
        , std::shared_ptr<std::mutex> &_sync_lock
        , std::shared_ptr<std::condition_variable> &_eof_cv
        , std::shared_ptr<double> &_next_chunk_send_time_ns
        , double _timestamp_tick
        , double _video_fps
        , std::shared_ptr<std::atomic<int>> &_eof_stream_counter
        , uint8_t _dscp) :
            CpuAffinity()
            , bit_rate(_bit_rate)
            , sample_rate(_sample_rate)
            , channels(_channels)
            , frame_size(_frame_size)
            , channel_layout(_channel_layout)
            , ptime_usec(_ptime_usec)
            , payload_type(_payload_type)
            , sdp_path(_sdp_path)
            , send_cb(_send_cb)
            , send_cv(_send_cv)
            , send_lock(_send_lock)
            , sync_cv(_sync_cv)
            , sync_lock(_sync_lock)
            , eof_cv(_eof_cv)
            , next_chunk_send_time_ns(_next_chunk_send_time_ns)
            , timestamp_tick(_timestamp_tick)
            , video_fps(_video_fps)
            , eof_stream_counter(_eof_stream_counter)
            , dscp(_dscp)
    { }

    int64_t bit_rate = 0;
    int sample_rate = 0;
    int channels = 0;
    int frame_size = 0;
    uint64_t channel_layout = 0;
    uint64_t ptime_usec = 0;
    int payload_type = 0;
    AVSampleFormat format = AV_SAMPLE_FMT_NONE;
    std::string sdp_path;
    std::shared_ptr<my_queue> send_cb;
    std::shared_ptr<std::condition_variable> send_cv;
    std::shared_ptr<std::mutex> send_lock;
    std::shared_ptr<std::condition_variable> sync_cv;
    std::shared_ptr<std::mutex> sync_lock;
    std::shared_ptr<std::condition_variable> eof_cv;
    std::shared_ptr<double> next_chunk_send_time_ns;
    double timestamp_tick = 0.0;
    double video_fps = 0;
    std::shared_ptr<std::atomic<int>> eof_stream_counter;
    uint8_t dscp = 0;
    size_t bit_depth_in_bytes = 0;
    void notify_all_cv() {
        eof_stream_counter->fetch_add(1);
        eof_cv->notify_one();
        send_cv->notify_all();
        sync_cv->notify_all();
    }
};

struct AudioReaderData: CpuAffinity
{
    AudioReaderData() = default;
    AudioReaderData(
        std::shared_ptr<AVFormatContext*> _p_format_context
        , AVCodec *_p_codec
        , AVCodecParameters *_p_codec_parameters
        , int _stream_index
        , std::string _file_path
        , std::shared_ptr<my_queue> &_conv_cb
        , std::shared_ptr<std::condition_variable> &_conv_cv
        , std::shared_ptr<std::mutex> &_conv_lock) :
            CpuAffinity()
            , p_format_context(_p_format_context)
            , p_codec(_p_codec)
            , p_codec_parameters(_p_codec_parameters)
            , stream_index(_stream_index)
            , file_path(_file_path)
            , conv_cb(_conv_cb)
            , conv_cv(_conv_cv)
            , conv_lock(_conv_lock)
    { }

    std::shared_ptr<AVFormatContext*> p_format_context = nullptr;
    const AVCodec *p_codec = nullptr;
    AVCodecParameters *p_codec_parameters = nullptr;
    int stream_index = -1;
    std::string file_path;
    std::shared_ptr<my_queue> conv_cb;
    std::shared_ptr<std::condition_variable> conv_cv;
    std::shared_ptr<std::mutex> conv_lock;
    const char *stream_name = "audio";
    const int ffmpeg_thread_count = 2;
    void notify_all_cv()
    {
        conv_cv->notify_all();
    }
};

struct AudioEncodeData
{
    AudioEncodeData(
        AudioRmaxData &rmax_data
        , std::shared_ptr<my_queue> &_conv_cb
        , std::shared_ptr<std::condition_variable> &_conv_cv
        , std::shared_ptr<std::mutex> &_conv_lock
        , const AVCodec *_p_codec
        , AVCodecParameters *_p_codec_parameters
        , int audio_stream_index, long cpu) :
            rmax_data(
                rmax_data.bit_rate
                , rmax_data.sample_rate
                , rmax_data.channels
                , rmax_data.frame_size
                , rmax_data.channel_layout
                , rmax_data.ptime_usec
                , rmax_data.payload_type
                , rmax_data.sdp_path
                , rmax_data.send_cb
                , rmax_data.send_cv
                , rmax_data.send_lock
                , rmax_data.sync_cv
                , rmax_data.sync_lock
                , rmax_data.eof_cv
                , rmax_data.next_chunk_send_time_ns
                , rmax_data.timestamp_tick
                , rmax_data.video_fps
                , rmax_data.eof_stream_counter
                , rmax_data.dscp)
                    , conv_cb(_conv_cb)
                    , conv_cv(_conv_cv)
                    , conv_lock(_conv_lock)
                    , p_codec(_p_codec)
                    , p_codec_parameters(_p_codec_parameters)
                    , stream_index(audio_stream_index)
    {
        rmax_data.set_cpu(cpu);
    }

    AudioRmaxData rmax_data;
    std::shared_ptr<my_queue> conv_cb;
    std::shared_ptr<std::condition_variable> conv_cv;
    std::shared_ptr<std::mutex> conv_lock;
    const AVCodec *p_codec = nullptr;
    AVCodecParameters *p_codec_parameters = nullptr;
    int stream_index = -1;
    void notity_all_cv()
    {
        conv_cv->notify_all();
        rmax_data.send_cv->notify_all();
        rmax_data.sync_cv->notify_all();
    }

public:
    struct rmax_cpu_set_t *affinity_mask_get()
    {
        return rmax_data.affinity_mask_get();
    }
};

struct AncillaryRmaxData
{
    AncillaryRmaxData() = default;
    AncillaryRmaxData(
        std::string &_sdp_path
        , AVPixelFormat _video_pix_format
        , double _fps
        , VIDEO_TYPE _video_type
        , int _video_sample_rate
        , int _payload_type
        , uint16_t _did
        , uint16_t _sdid
        , uint16_t _video_width
        , uint16_t _video_height
        , int64_t _video_duration
        , std::shared_ptr<double>& _next_chunk_send_time_ns
        , std::shared_ptr<std::atomic<int>> &_eof_stream_counter
        , std::shared_ptr<std::condition_variable> &_sync_cv
        , std::shared_ptr<std::mutex> &_sync_lock
        , std::shared_ptr<std::condition_variable> &_eof_cv) :
            video_pix_format(_video_pix_format)
            , fps(_fps)
            , video_type(_video_type)
            , video_sample_rate(_video_sample_rate)
            , payload_type(_payload_type)
            , did(_did)
            , sdid(_sdid)
            , video_width(_video_width)
            , video_height(_video_height)
            , video_duration_sec(_video_duration)
            , sdp_path(_sdp_path)
            , next_chunk_send_time_ns(_next_chunk_send_time_ns)
            , eof_stream_counter(_eof_stream_counter)
            , sync_cv(_sync_cv)
            , sync_lock(_sync_lock)
            , eof_cv(_eof_cv)
    { }
    AVPixelFormat video_pix_format = AV_PIX_FMT_NONE;
    double fps = 0;
    VIDEO_TYPE video_type = VIDEO_TYPE::NON_VIDEO;
    int video_sample_rate = 0;
    int payload_type = 0;
    uint16_t did = 0;
    uint16_t sdid = 0;
    uint16_t video_width = 0;
    uint16_t video_height = 0;
    int64_t video_duration_sec = 0;
    std::string sdp_path;
    std::shared_ptr<double> next_chunk_send_time_ns;
    std::shared_ptr<std::atomic<int>> eof_stream_counter;
    std::shared_ptr<std::condition_variable> sync_cv;
    std::shared_ptr<std::mutex> sync_lock;
    std::shared_ptr<std::condition_variable> eof_cv;
    void notify_all_cv()
    {
        eof_stream_counter->fetch_add(1);
        eof_cv->notify_one();
        sync_cv->notify_all();
    }
};

struct MediaData {
    uint16_t              dst_port = 0;
    // video and audio
    int                   sample_rate = 0;
    uint8_t               payload_type = 0;
    // video related parameters
    double                fps = 0.0;
    uint16_t              width = 0;
    uint16_t              height = 0;
    TP_MODE               tp_mode = TPNL;
    VIDEO_TYPE            video_type = PROGRESSIVE;
    // audio related parameters
    uint32_t              audio_ptime_us = 0;
    int                   channels_num = 0;
    uint16_t              bit_depth = 0;
    // ancillary related parameters
    uint16_t              did = 0;
    uint16_t              sdid = 0;
    uint16_t              cmax = 0;
    uint32_t              bw_as = 0;
    uint16_t              depth = 0;
    SAMPLING_TYPE sampling = SAMPLING_TYPE::YCBCR422;
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



struct SendData
{
    int packet_counter;
    int px_grp_left_in_frame_field;
    uint16_t px_grp_left_in_line;
};

struct av_deleter
{
    void operator()(AVCodecContext* thing)
    {
        avcodec_free_context(&thing);
    }
};

void calculate_stream_time(eMediaType_t stream_type, std::shared_ptr<double>& time_ns, cst_data& data, double* p_timestamp_tick)
{
    double t_frame_ns = (double)nanoseconds{seconds{1}}.count() / data.fps;
    uint64_t N = (uint64_t)(*time_ns/t_frame_ns + 1);
    double first_packet_start_time_ns = N * t_frame_ns; //next alignment point calculation

    if (video & stream_type) {
        int packets_in_frame = 0;
        if (data.pix_format == AVPixelFormat::AV_PIX_FMT_YUV422P10LE) {
            packets_in_frame = data.width == FHD_WIDTH ? HD_PACKETS_PER_FRAME_422_10B : UHD_PACKETS_PER_FRAME_422_10B;
        } else {  // must be 8 bits
            packets_in_frame = data.width == FHD_WIDTH ? HD_PACKETS_PER_FRAME_422_8B : UHD_PACKETS_PER_FRAME_422_8B;
        }

        double r_active;
        double tro_default_multiplier = 0;
        if (data.video_type == VIDEO_TYPE::PROGRESSIVE) {
            r_active = (1080.0 / 1125.0);
            if (data.height >= FHD_HEIGHT) { // As defined by SMPTE 2110-21 6.3.2
                tro_default_multiplier = (43.0 / 1125.0);
            } else {
                tro_default_multiplier = (28.0 / 750.0);
            }
        } else {
            if (data.height >= FHD_HEIGHT) { // As defined by SMPTE 2110-21 6.3.3
                r_active = (1080.0 / 1125.0);
                tro_default_multiplier = (22.0 / 1125.0);
            } else if (data.height >= 576) {
                r_active = (576.0 / 625.0);
                tro_default_multiplier = (26.0 / 625.0);
            } else {
                r_active = (487.0 / 525.0);
                tro_default_multiplier = (20.0 / 525.0);
            }
        }

        double trs_ns = (t_frame_ns * r_active) / packets_in_frame;
        double tro = (tro_default_multiplier * t_frame_ns) - (video_tro_default_modification * trs_ns);
        first_packet_start_time_ns += tro;

    }
    if (p_timestamp_tick) {
        *p_timestamp_tick = time_to_rtp_timestamp(first_packet_start_time_ns, data.sample_rate);
    }

    *time_ns = first_packet_start_time_ns;
}

struct RtpAudioHeaderBuilder
{
    RtpAudioHeaderBuilder(
        size_t payload_size
        , uint8_t payload_type
        , size_t strides_in_chunk
        , size_t sample_rate
        , size_t ptime_usec
        , uint16_t packet_stride_size
        , size_t samples_in_packet
        , size_t num_of_channels
        , size_t num_of_samples_in_av_packet
        , size_t bit_depth_in_bytes
        , double timestamp_tick) :
            m_payload_size(payload_size)
            , m_payload_type(payload_type)
            , m_strides_in_chunk(strides_in_chunk)
            , m_sample_rate(sample_rate)
            , m_ptime_usec(ptime_usec)
            , m_packet_stride_size(packet_stride_size)
            , m_samples_in_stride(samples_in_packet)
            , m_num_of_channels(num_of_channels)
            , m_timestamp_tick(timestamp_tick)
            , m_num_of_samples_in_av_packet(num_of_samples_in_av_packet)
            , m_bit_depth_in_bytes(bit_depth_in_bytes)
    { }

    void fill_chunk(uint8_t *buff, std::shared_ptr<AVPacket> sptr_av_packet_arr[]);
    uint32_t m_seq_num = 0;
    const size_t m_payload_size;
    const uint8_t m_payload_type;
    const size_t m_strides_in_chunk;
    const size_t m_sample_rate;
    const size_t m_ptime_usec;
    uint16_t m_packet_stride_size;
    const size_t m_samples_in_stride;
    const size_t m_num_of_channels;
    double m_timestamp_tick;
    const size_t m_num_of_samples_in_av_packet;
    const size_t m_bit_depth_in_bytes;
};

void RtpAudioHeaderBuilder::fill_chunk(uint8_t *buff, std::shared_ptr<AVPacket> sptr_av_packet_arr[])
{
    uint8_t *pBuff_8 = buff;
    size_t av_packet_index = 0;
    uint8_t *src = &(sptr_av_packet_arr[av_packet_index].get())->data[0];
    size_t num_of_samples_left_in_chunk = m_num_of_samples_in_av_packet;

    for (size_t m_strides_index = 0; m_strides_index < m_strides_in_chunk; ++m_strides_index, pBuff_8 += m_packet_stride_size) {

        // build RTP header - 12 bytes
        /*
         0                   1                   2                   3
         0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
         +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         | V |P|X|  CC   |M|     PT      |                      SEQ                     |
         +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         |                                    timestamp                                         |
         +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         |                                         ssrc                                               |
         +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+*/

        rtp_header *p_rtp_header = (rtp_header*)pBuff_8;
        p_rtp_header->version = 2;
        p_rtp_header->extension = 0;
        p_rtp_header->cc = 0;
        p_rtp_header->marker = 0;
        p_rtp_header->payload_type = m_payload_type;
        p_rtp_header->sequence_number = htobe16((uint16_t)m_seq_num);
        ++m_seq_num;
        p_rtp_header->timestamp = htobe32((uint32_t)m_timestamp_tick);
        m_timestamp_tick += (double)m_samples_in_stride;
        p_rtp_header->ssrc = 0x0eb51dbe; // simulated ssrc

        uint8_t *dst = pBuff_8 + sizeof(*p_rtp_header);

        if (num_of_samples_left_in_chunk >= m_samples_in_stride) {
            memcpy(dst, src, m_payload_size);
            num_of_samples_left_in_chunk -= m_samples_in_stride;
            src += m_payload_size;
        } else {
            //Copy leftovers samples
            size_t size_to_copy = num_of_samples_left_in_chunk * m_num_of_channels * m_bit_depth_in_bytes;
            memcpy(dst, src, size_to_copy);
            dst += size_to_copy;

            //Copy samples from next av packet
            ++av_packet_index;
            src = &(sptr_av_packet_arr[av_packet_index].get())->data[0];
            size_to_copy = (m_samples_in_stride - num_of_samples_left_in_chunk) * m_num_of_channels * m_bit_depth_in_bytes;
            memcpy(dst, src, size_to_copy);
            src += size_to_copy;
            num_of_samples_left_in_chunk = (m_num_of_samples_in_av_packet - (m_samples_in_stride - num_of_samples_left_in_chunk));
        }
    }
}

struct RtpAncillaryHeaderBuilder
{
    RtpAncillaryHeaderBuilder(
        double fps
        , uint8_t payload_type
        , uint16_t payload_size
        , uint16_t did
        , uint16_t sdid
        , size_t strides_in_chunk
        , uint16_t packet_stride_size
        , size_t samples_in_packet
        , VIDEO_TYPE video_type) :
            m_fps(fps)
            , m_payload_type(payload_type)
            , m_payload_size(payload_size)
            , m_did(did)
            , m_sdid(sdid)
            , m_strides_in_chunk(strides_in_chunk)
            , m_packet_stride_size(packet_stride_size)
            , m_samples_in_stride(samples_in_packet)
            , m_video_type(video_type)
    {
        if (m_video_type == VIDEO_TYPE::INTERLACE) {
            m_field = interlace_first_field_value;
        }
    }

    void fill_chunk(uint8_t *buff, uint16_t *payload_sizes_ptr, double send_time_ns);
    double m_fps = 0;
    uint32_t m_seq_num = 0;
    const uint8_t m_payload_type = 0;
    const uint16_t m_payload_size = 0;
    const uint16_t m_did = 0;
    const uint16_t m_sdid = 0;
    const size_t m_strides_in_chunk = 0;
    uint16_t m_packet_stride_size = 0;
    const size_t m_samples_in_stride = 0;
    VIDEO_TYPE m_video_type = VIDEO_TYPE::NON_VIDEO;
    uint8_t m_field = progresive_field_value;

private:
    const static uint8_t progresive_field_value = 0b00;
    const static uint8_t interlace_first_field_value = 0b10;
    const static uint8_t interlace_second_field_value = 0b11;
};

uint32_t number_of_set_bits(uint32_t i)
{
    i = i - ((i >> 1) & 0x55555555);
    i = (i & 0x33333333) + ((i >> 2) & 0x33333333);
    return (((i + (i >> 4)) & 0x0F0F0F0F) * 0x01010101) >> 24;
}

uint8_t get_8bit_val(uint32_t number)
{
    return (uint8_t)((number >> 8) & 0x1);
}

uint8_t even_parity(uint32_t number)
{
    return (uint8_t)(number_of_set_bits(number) & 0x1);
}

#ifdef _MSC_VER
#define PACK( __Declaration__ ) __pragma( pack(push, 1) ) __Declaration__ __pragma( pack(pop) )
#elif defined(__GNUC__)
#define PACK( __Declaration__ ) __Declaration__ __attribute__((__packed__))
#endif

PACK(struct ancillary_rtp_header {
    rtp_header s_rtp_hader;
    uint16_t extended_sequence_number;  // Extended Sequence Number: 16 bits
    uint16_t length;  // Length: 16 bits
    uint8_t anc_count;  // ANC_Count: 8 bits
    uint32_t reserved_16_to_21_6bit : 6;  // reserved: 6 of 22 bits
    uint32_t f : 2;  // F: 2 bits
    uint32_t reserved_0_to_15_16bit : 16;  // reserved: 16 of 22 bits
});

PACK(struct ancillary_rtp_data_header {
    uint32_t line_number_4_to_10_7bit : 7;  // Line_Number: 7 of 11 bits
    uint32_t c : 1;  // C: 1 bit

    uint32_t horizontal_offset_8_to_11_4bit : 4;  // Horizontal_Offset: 4 of 12 bits
    uint32_t line_number_0_to_3_4bit : 4;  // Line_Number: 4 of 11 bits

    uint32_t horizontal_offset_0_to_7_8bit : 8;  // Horizontal_Offset: 12 bits

    uint32_t stream_num : 7;  // StreamNum: 7 bits
    uint32_t s : 1; //S (Data Stream Flag): 1 bit

    uint32_t did_2_to_9_8bit : 8;  // Data Identification Word DID: 8 of 10 bits
    uint32_t sdid_4_to_9_6bit : 6; // Secondary Data Identification Word SDID: 6 of 10 bits
    uint32_t did_0_to_1_2bit : 2;  // Data Identification Word DID: 2 of 10 bits

    uint32_t data_count_6_to_7_2bit : 2;  // Data_Count: 2 of 10 bits
    uint32_t data_count_8_1bit_even_parity : 1;
    uint32_t data_count_9_1bit_not_8_bit : 1;
    uint32_t sdid_0_to_3_4bit : 4; // Secondary Data Identification Word SDID: 4 of 10 bits

    uint32_t user_data_world1_8_1bit_even_parity : 1;
    uint32_t user_data_world19_1bit_not_8_bit : 1;
    uint32_t data_count_0_to_5_6bit : 6;  // Data_Count: 6 of 10 bits

    uint32_t user_data_world1_0_to_7_9bit : 8;  // User_Data_Words: integer number of 8 of 10 bit words

    uint32_t checksum_2_to_7_6bit : 6;  // checksum: integer number of 8 of 10 bit words
    uint32_t checksum_8_to_8_1bit : 1;  // checksum: integer number of 1 of 10 bit words
    uint32_t checksum_9_to_9_1bit_not_8_bit : 1;  // checksum: integer number of 1 of 10 bit words

    uint32_t word_align1_6bit : 6;  // word_align1: integer number of 6 of 6 bit words
    uint32_t checksum_0_to_1_2bit : 2;  // checksum: integer number of 2 of 10 bit words

    uint32_t word_align2_8bit : 8;  // word_align2: integer number of 8 of 8 bit words

    inline void set_line_number(uint16_t _line_number)
    {
        line_number_0_to_3_4bit = (0x000F & _line_number);
        line_number_4_to_10_7bit = (0x007F & (_line_number >> 4));
    }

    inline void set_horizontal_offset(uint16_t _horizontal_offset)
    {
        horizontal_offset_0_to_7_8bit = (0x00FF & _horizontal_offset);
        horizontal_offset_8_to_11_4bit = (0x000F & (_horizontal_offset >> 8));
    }

    inline uint16_t did_sdid_add_parity(uint16_t _val)
    {
        _val &= 0xff;
        uint8_t parity = even_parity(_val);

        _val |= ((!parity << 1) | parity) << 8;
        return _val;
    }

    inline void set_did(uint16_t _did)
    {
        _did = did_sdid_add_parity(_did);
        did_0_to_1_2bit = (0x0003 & _did);
        did_2_to_9_8bit = (0x00FF & (_did >> 2));
    }

    inline uint16_t get_did()
    {
        return (((uint16_t)did_0_to_1_2bit)) | (((uint16_t)did_2_to_9_8bit) << 2);
    }

    inline void set_sdid(uint16_t _sdid)
    {
        _sdid = did_sdid_add_parity(_sdid);
        sdid_0_to_3_4bit = (0x000F & _sdid);
        sdid_4_to_9_6bit = (0x003F & (_sdid >> 4));
    }

    inline uint16_t get_sdid()
    {
        return (((uint16_t)sdid_0_to_3_4bit)) | (((uint16_t)sdid_4_to_9_6bit) << 4);
    }

    inline void set_data_count(uint8_t _data_count)
    {
        data_count_0_to_5_6bit = (0x3F & (uint16_t)_data_count);
        data_count_6_to_7_2bit = (0x03 & (_data_count >> 6));
        data_count_8_1bit_even_parity = even_parity(get_data_count());
        data_count_9_1bit_not_8_bit = !data_count_8_1bit_even_parity;
    }

    inline uint16_t get_data_count()
    {
        return (((uint16_t)data_count_0_to_5_6bit)) | (((uint16_t)data_count_6_to_7_2bit) << 6);
    }

    inline uint16_t set_user_data_1_and_checksum()
    { // SMPTE ST 291-1 Ancillary Data
        // Set user data
        user_data_world1_8_1bit_even_parity = even_parity(VIDEO_FORMAT); // 2 - 010    16:9 video format
        user_data_world19_1bit_not_8_bit = !user_data_world1_8_1bit_even_parity;
        user_data_world1_0_to_7_9bit = (uint8_t)VIDEO_FORMAT;

        //Set checksum
        uint16_t cs_temp = VIDEO_FORMAT + get_did() + get_sdid() + get_data_count();
        checksum_8_to_8_1bit = get_8bit_val(cs_temp);
        checksum_9_to_9_1bit_not_8_bit = !checksum_8_to_8_1bit;
        checksum_0_to_1_2bit = (uint8_t)cs_temp;
        checksum_2_to_7_6bit = 0x3F & ((uint8_t)cs_temp >> 2);
        return 4;
    }
});

void RtpAncillaryHeaderBuilder::fill_chunk(uint8_t *buff, uint16_t *payload_sizes_ptr, double send_time_ns)
{
    for (size_t m_strides_index = 0; m_strides_index < m_strides_in_chunk; ++m_strides_index,
            buff += m_packet_stride_size) {
        memset(buff, 0, m_packet_stride_size);
        ancillary_rtp_header* p_anc_rtp_hdr = (struct ancillary_rtp_header*)&buff[0];
        //RTP header initialization
        p_anc_rtp_hdr->s_rtp_hader.version = 2;
        p_anc_rtp_hdr->s_rtp_hader.extension = 0;
        p_anc_rtp_hdr->s_rtp_hader.cc = 0;
        p_anc_rtp_hdr->s_rtp_hader.marker = 1;
        p_anc_rtp_hdr->s_rtp_hader.payload_type = m_payload_type;
        p_anc_rtp_hdr->s_rtp_hader.sequence_number = htobe16((uint16_t)m_seq_num);
        p_anc_rtp_hdr->s_rtp_hader.timestamp = htobe32((uint32_t)time_to_rtp_timestamp(send_time_ns, 90000));
        p_anc_rtp_hdr->f = m_field;

        if (m_video_type != VIDEO_TYPE::PROGRESSIVE) {
            m_field = (interlace_first_field_value == m_field)? interlace_second_field_value : interlace_first_field_value;
        }

        p_anc_rtp_hdr->s_rtp_hader.ssrc = htobe32(0x0eb51dbf);

        //Payload Header initialization
        p_anc_rtp_hdr->extended_sequence_number = htobe16((uint16_t)(m_seq_num>>16));
        ++m_seq_num;
        p_anc_rtp_hdr->anc_count = 1;

        ancillary_rtp_data_header* p_rtp_data_hdr = (struct ancillary_rtp_data_header*)&buff[20];
        p_rtp_data_hdr->c = 0;
        p_rtp_data_hdr->set_line_number(10);
        p_rtp_data_hdr->set_horizontal_offset(11);
        p_rtp_data_hdr->s = 0;
        p_rtp_data_hdr->stream_num = 0;
        p_rtp_data_hdr->set_did(m_did);
        p_rtp_data_hdr->set_sdid(m_sdid);
        p_rtp_data_hdr->set_data_count(1);
        uint16_t length = 8 + p_rtp_data_hdr->set_user_data_1_and_checksum();
        p_anc_rtp_hdr->length = htobe16(length);
        payload_sizes_ptr[m_strides_index] = 20 /*RTP + ERTP headers*/ + length;
    }
}

struct RtpVideoHeaderBuilder
{
    RtpVideoHeaderBuilder(int px_height, int px_width, int packets_in_frame_field, uint16_t bit_size,
                          uint16_t px_grp_size, double fps, uint8_t payload_type, double timestamp_tick,
                          std::vector<uint16_t> &sizes,
                          VIDEO_TYPE video_type, AVPixelFormat pix_format)
    : m_px_height(px_height)
    , m_px_width(px_width)
    , m_packets_in_frame_field(packets_in_frame_field)
    , m_bit_depth(bit_size)
    , m_grp_size(px_grp_size)
    , m_px_grp_in_line(px_width / PX_IN_422_GRP)
    , m_fps(fps)
    , m_payload_type(payload_type)
    , m_timestamp_tick(timestamp_tick)
    , m_sizes(std::move(sizes))
    , m_video_type(video_type)
    , m_pix_format(pix_format)
    { }
    void set_counters() {
        m_Y_counter = 0;
        m_Cb_counter = 0;
        m_Cr_counter = 0;
        if ((m_video_type != VIDEO_TYPE::PROGRESSIVE) && (m_field)) {
            jump_to_next_line_interlace_logic();
        }
    }

    void jump_to_next_line_interlace_logic() {
        if (m_pix_format == AVPixelFormat::AV_PIX_FMT_YUV422P ||
            m_pix_format == AVPixelFormat::AV_PIX_FMT_YUV422P10LE) {
            m_Cb_counter += m_px_grp_in_line;
            m_Y_counter += m_px_grp_in_line;
            m_Cr_counter += m_px_grp_in_line;
            m_Y_counter += m_px_grp_in_line;
        }
        else if (m_pix_format == AVPixelFormat::AV_PIX_FMT_UYVY422) {
            m_Y_counter += (m_px_grp_in_line * m_grp_size);
        }
        else {
            std::cerr << "unsupported pixel format\n";
            throw std::runtime_error("unsupported pixel format");
        }
    }

    void fill_packet(uint8_t *buff, SendData &sd, AVFrame *av_frame);
    uint32_t m_seq_num = 0;
    int m_px_height;
    int m_px_width;
    int m_packets_in_frame_field;
    uint16_t m_bit_depth;
    uint16_t m_srd_offset = 0;
    uint16_t m_grp_size;
    uint16_t m_px_grp_in_line;
    double m_fps;
    uint8_t m_payload_type;
    double m_timestamp_tick;
    uint8_t *m_Y = nullptr;
    uint8_t *m_Cb = nullptr;
    uint8_t *m_Cr = nullptr;
    uint32_t m_Y_counter = 0;
    uint32_t m_Cb_counter = 0;
    uint32_t m_Cr_counter = 0;
    uint16_t m_curr_line = 0;
    std::vector<uint16_t> m_sizes;
    VIDEO_TYPE m_video_type = VIDEO_TYPE::NON_VIDEO;
    bool m_field = false;
    AVPixelFormat m_pix_format = AV_PIX_FMT_NONE;
};

void RtpVideoHeaderBuilder::fill_packet(uint8_t *buff, SendData &sd, AVFrame *av_frame)
{
    // build RTP header - 12 bytes
    /*
    0                   1                   2                   3
    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    | V |P|X|  CC   |M|     PT      |            SEQ                |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |                           timestamp                           |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |                           ssrc                                |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+*/
    rtp_header* p_rtp_header = (rtp_header*)buff;
    p_rtp_header->version = 2;
    p_rtp_header->extension = 0;
    p_rtp_header->cc = 0;
    p_rtp_header->timestamp = htobe32((uint32_t)m_timestamp_tick);
    if (sd.packet_counter == m_packets_in_frame_field - 1) {
        p_rtp_header->marker = 1;

        double ticks = (90000.0 / m_fps);
        if (m_video_type != VIDEO_TYPE::PROGRESSIVE) {
            ticks /= 2;
        }
        m_timestamp_tick += ticks;
    } else {
        p_rtp_header->marker = 0;
    }
    p_rtp_header->payload_type = m_payload_type;
    p_rtp_header->sequence_number = htobe16((uint16_t)m_seq_num);
    p_rtp_header->ssrc = 0x0eb51dbd; // simulated ssrc

    // build SRD header - 8-14 bytes
    /*
    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |    Extended Sequence Number   |           SRD Length          |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |F|     SRD Row Number          |C|         SRD Offset          |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+*/
    void *buffer = ++p_rtp_header;
    *(uint16_t *)buffer = htobe16((uint16_t )(m_seq_num >> 16));
    ++m_seq_num;
    srd_header *srd = reinterpret_cast<srd_header*>(reinterpret_cast<uint8_t*>(buffer) +
        SIZE_OF_EXTENSION_SEQ);
    int data_offset = sizeof(rtp_header) + SIZE_OF_EXTENSION_SEQ + sizeof(srd_header);
    int payload_size = m_sizes[sd.packet_counter] - data_offset;
    // check how many SRD we need
    srd->set_srd_row_number(m_curr_line);
    srd->set_srd_offset(m_srd_offset);
    int payload_sz_with_2_srds = (payload_size - sizeof(srd_header)) / m_grp_size * m_grp_size;
    int copy_size = payload_size;
    if ((sd.px_grp_left_in_line < payload_size / m_grp_size) &&
        (sd.px_grp_left_in_frame_field > sd.px_grp_left_in_line) &&
        ((payload_sz_with_2_srds - (sd.px_grp_left_in_line * m_grp_size)) >= m_grp_size)) {
        // this handles case when two srds are needed since line has less px then payload can contain
        payload_size -= sizeof(srd_header);
        payload_size = payload_size / m_grp_size * m_grp_size;
        copy_size = payload_size;
        uint16_t px_sizes = sd.px_grp_left_in_line * m_grp_size;
        srd->srd_length = htobe16(px_sizes);
        payload_size -= px_sizes;
        sd.px_grp_left_in_frame_field -= sd.px_grp_left_in_line;
        sd.px_grp_left_in_line = 0;
        srd->c = 1;
        srd->f = m_field;
        srd++;
        px_sizes = payload_size / m_grp_size * m_grp_size;
        srd->srd_length = htobe16(px_sizes);
        m_curr_line = (m_curr_line + 1) % m_px_height;
        srd->set_srd_row_number(m_curr_line);
        srd->set_srd_offset(0);
        srd->c = 0;
        srd->f = m_field;
        sd.px_grp_left_in_frame_field -= (payload_size / m_grp_size);
        sd.px_grp_left_in_line = m_px_grp_in_line - payload_size / m_grp_size;
        m_srd_offset = ((m_px_grp_in_line - sd.px_grp_left_in_line) * m_grp_size *
            BITS_IN_BYTES / m_bit_depth / PX_IN_422_GRP) % m_px_width;
        data_offset += sizeof(srd_header);
    } else {
        // handle cases of one SRD
        payload_size = payload_size / m_grp_size * m_grp_size;
        srd->c = 0;
        srd->f = m_field;
        // last packet in frame/field
        if (unlikely(sd.px_grp_left_in_frame_field <= payload_size / m_grp_size)) {
            copy_size = sd.px_grp_left_in_frame_field * m_grp_size;
            srd->srd_length = htobe16((uint16_t)copy_size);
            m_curr_line = 0;
            m_srd_offset = 0;
            if (unlikely(sd.px_grp_left_in_frame_field < payload_size / m_grp_size)) {
                memset(&buff[data_offset] + copy_size, 0, payload_size - copy_size);
            }
        } else {
            copy_size = payload_size;
            srd->srd_length = htobe16(payload_size);
            sd.px_grp_left_in_line -= payload_size / m_grp_size;
            sd.px_grp_left_in_frame_field -= payload_size / m_grp_size;
            m_srd_offset = ((m_px_grp_in_line - sd.px_grp_left_in_line) * m_grp_size *
                BITS_IN_BYTES / m_bit_depth / PX_IN_422_GRP) % m_px_width;
            if (!sd.px_grp_left_in_line) {
                m_curr_line = (m_curr_line + 1) % m_px_height;
                sd.px_grp_left_in_line = m_px_grp_in_line;
                m_srd_offset = 0;
            }
        }
    }
    // copy data from avFrame to buffer
    if (av_frame->format == AVPixelFormat::AV_PIX_FMT_YUV422P) {
        int offset = data_offset;
        while (data_offset < copy_size + offset) {
            buff[data_offset++] = m_Cb[m_Cb_counter++];
            buff[data_offset++] = m_Y[m_Y_counter++];
            buff[data_offset++] = m_Cr[m_Cr_counter++];
            buff[data_offset++] = m_Y[m_Y_counter++];

            if ((m_video_type != VIDEO_TYPE::PROGRESSIVE) && !(m_Cb_counter % m_px_grp_in_line)) {
                jump_to_next_line_interlace_logic();
            }
        }
    }
    else if (av_frame->format == AVPixelFormat::AV_PIX_FMT_UYVY422) {

        if (m_video_type == VIDEO_TYPE::PROGRESSIVE) {
            memcpy(&buff[data_offset], &m_Y[m_Y_counter], copy_size);
            m_Y_counter += copy_size;
        } else {
            uint32_t size_to_copy = (m_Y_counter + copy_size) % m_px_grp_in_line;
            memcpy(&buff[data_offset], &m_Y[m_Y_counter], size_to_copy);
            m_Y_counter += size_to_copy;

            if (!(m_Y_counter % m_px_grp_in_line) ) {
                jump_to_next_line_interlace_logic();
            }

            if (copy_size - size_to_copy) {
                memcpy(&buff[data_offset + size_to_copy], &m_Y[m_Y_counter], copy_size - size_to_copy);
                m_Y_counter += (copy_size - size_to_copy);
                if (!(m_Y_counter % m_px_grp_in_line) ) {
                    jump_to_next_line_interlace_logic();
                }
            }
        }
    }
    else if (av_frame->format == AVPixelFormat::AV_PIX_FMT_YUV422P10LE) {
        int offset = data_offset;

        while (data_offset < copy_size + offset) {
            uint16_t *cb = reinterpret_cast<uint16_t*>(m_Cb);
            uint16_t *cr = reinterpret_cast<uint16_t*>(m_Cr);
            uint16_t *y = reinterpret_cast<uint16_t*>(m_Y);

            buff[data_offset] = (cb[m_Cb_counter]) >> 2;
            buff[data_offset + 1] = (((cb[m_Cb_counter]) << 6) & 0xc0) | (y[m_Y_counter] >> 4);
            buff[data_offset + 2] = (((y[m_Y_counter]) << 4) & 0xf0) | ((cr[m_Cr_counter]) >> 6);
            ++m_Y_counter;
            buff[data_offset + 3] = (((cr[m_Cr_counter]) << 2) & 0xfc) | ((y[m_Y_counter]) >> 8);
            buff[data_offset + 4] = ((y[m_Y_counter]) & 0xff);
            ++m_Y_counter;
            ++m_Cb_counter;
            ++m_Cr_counter;
            data_offset += 5;

            if ((m_video_type != VIDEO_TYPE::PROGRESSIVE) && !(m_Cr_counter % m_px_grp_in_line) ) {
                jump_to_next_line_interlace_logic();
            }
        }
    }
    else {
        std::cerr << "unsupported pixel format\n";
        throw std::runtime_error("unsupported pixel format");
    }
}

void AVFrameDeleter(AVFrame* f)
{
    av_frame_free(&f);
}

void AVPacketDeleter(AVPacket* p)
{
    av_packet_unref(p);
    delete p;
}

void AVSubtitleDeleter(AVSubtitle* s)
{
    avsubtitle_free(s);
    delete s;
}

struct SynchronizerData
{
    SynchronizerData() = default;
    SynchronizerData(
        std::shared_ptr<std::condition_variable> &_sync_cv
        , std::shared_ptr<std::condition_variable> &_eof_cv
        , std::shared_ptr<std::atomic<int>> &_eof_stream_counter) :
            sync_cv(_sync_cv)
            , eof_cv(_eof_cv)
            , eof_stream_counter(_eof_stream_counter)
    { }

    std::shared_ptr<std::condition_variable> sync_cv;
    std::shared_ptr<std::condition_variable> eof_cv;
    std::shared_ptr<double> video_next_frame_field_send_time_ns;
    std::shared_ptr<double> audio_next_chunk_send_time_ns;
    std::shared_ptr<double> ancillary_next_chunk_send_time_ns;
    std::shared_ptr<std::atomic<int>> eof_stream_counter;
    int number_of_streams = 0;

    static void streams_synchronizer(SynchronizerData data)
    {
        std::mutex lck;
        std::unique_lock<std::mutex> lock(lck);
        uint32_t loop_counter = 0;

        while (likely(!exit_app()) && run_threads) {
            data.eof_cv->wait(lock, [&data]{return (*data.eof_stream_counter) == data.number_of_streams;});

            double v_time_ns = 0;
            if (data.video_next_frame_field_send_time_ns) {
                v_time_ns = *data.video_next_frame_field_send_time_ns;
            }

            double a_time_ns = 0;
            if (data.audio_next_chunk_send_time_ns) {
                a_time_ns = *data.audio_next_chunk_send_time_ns;
            }

            double time_ns = std::max<double>(v_time_ns, a_time_ns) + (double)nanoseconds{milliseconds{100}}.count();

            if (data.video_next_frame_field_send_time_ns) {
                *data.video_next_frame_field_send_time_ns = time_ns;
            }

            if (data.audio_next_chunk_send_time_ns) {
                *data.audio_next_chunk_send_time_ns = time_ns;
            }

            if (data.ancillary_next_chunk_send_time_ns) {
                *data.ancillary_next_chunk_send_time_ns = time_ns;
            }

            std::cout << "End of loop #" << ++loop_counter << std::endl;
            *data.eof_stream_counter = 0;
            data.sync_cv->notify_all();
        }
        std::cout << "The synchronizer is closing ..." << std::endl;
    }

public:
    void add_stream()
    {
        number_of_streams++;
    }
};

void go_to_sleep(uint64_t next_time_to_wake_up_ns, uint64_t time_to_wake_before_ns)
{
    uint64_t sleep_for_ns = next_time_to_wake_up_ns - get_tai_time_ns() - time_to_wake_before_ns;
    if (next_time_to_wake_up_ns > (get_tai_time_ns() + time_to_wake_before_ns)) {
        std::this_thread::sleep_for(nanoseconds(sleep_for_ns));
    }
}

void rivermax_ancillary_sender(AncillaryRmaxData data)
{
    if (unlikely(!run_threads)) {
        return;
    }
    const size_t num_of_chunks = 100 * (size_t)data.fps;
    const size_t samples_in_stride = 1;
    const size_t strides_in_chunk = 1;
    const size_t payload_size = 236;
    const size_t payload_size_with_rtp = payload_size + 20;  // ERTP header size
    const size_t packet_stride_size = river_align_up_pow2(payload_size_with_rtp, get_cache_line_size()); // align to cache line

    rmax_mem_block block;
    memset(&block, 0, sizeof(block));

    block.chunks_num = num_of_chunks;
    block.data_size_arr = nullptr; //data_size_arr must be set to NULL in dynamic mode

    rmax_buffer_attr buffer_attr;
    memset(&buffer_attr, 0, sizeof(buffer_attr));

    rmax_qos_attr qos;
    memset(&qos, 0, sizeof(qos));

    buffer_attr.chunk_size_in_strides = strides_in_chunk;
    buffer_attr.mem_block_array = &block;
    buffer_attr.mem_block_array_len = 1;
    buffer_attr.data_stride_size = (uint16_t)packet_stride_size;
    buffer_attr.app_hdr_stride_size = 0;

    std::ifstream is(data.sdp_path);
    std::string sdp_cont((std::istreambuf_iterator<char>(is)),
        std::istreambuf_iterator<char>());

    double video_frame_field_time_interval_ns = ((double)nanoseconds{seconds{1}}.count())/data.fps;
    uint32_t frames_fields_per_sec = (uint32_t)data.fps;
    uint32_t frames_fields_per_video = (uint32_t)(data.fps * data.video_duration_sec);
    if (data.video_type != VIDEO_TYPE::PROGRESSIVE) {
        video_frame_field_time_interval_ns /= 2;
        frames_fields_per_video *= 2;
        frames_fields_per_sec *= 2;
    }

    rmax_stream_id stream_id;
    rmax_status_t status = rmax_out_create_stream(const_cast<char*>(sdp_cont.c_str()),
        &buffer_attr, &qos, 1, 2 /* ancillary stream index */, &stream_id);
    if (status != RMAX_OK) {
        std::cerr << "failed creating ancillary output stream, got status:" << status;
        run_threads = false;
        data.notify_all_cv();
        return;
    }
    std::cout<<"ancillary stream created with ID "<<stream_id<<std::endl;
    RtpAncillaryHeaderBuilder chunk_builder = RtpAncillaryHeaderBuilder(
        data.fps
        , data.payload_type
        , payload_size
        , data.did
        , data.sdid
        , strides_in_chunk
        , (uint16_t)packet_stride_size
        , samples_in_stride
        , data.video_type
    );

    go_to_sleep((uint64_t)*data.next_chunk_send_time_ns, (uint64_t)nanoseconds{seconds{1}}.count());
    rmax_commit_flags_t c_flags{};

    std::cout << "Ancillary sender is on!" << std::endl;

    while (likely(!exit_app()) && run_threads) {
        double start_send_time_ns = *data.next_chunk_send_time_ns;
        for (uint32_t frame_field_index = 0; frame_field_index < frames_fields_per_video; ++frame_field_index) {
            *data.next_chunk_send_time_ns = start_send_time_ns + frame_field_index * video_frame_field_time_interval_ns;
            if (0 == ((frame_field_index + 1) % frames_fields_per_sec)) {
                // wait for ancillary time instead of sending massive chunks to rivermax
                go_to_sleep((uint64_t)*data.next_chunk_send_time_ns, (uint64_t)nanoseconds{milliseconds{ancillary_wakeup_delta_ms}}.count());
            }

            void *chunk_buffer = nullptr;
            uint16_t *payload_sizes_ptr = nullptr;
            do {
                status = rmax_out_get_next_chunk_dynamic(stream_id,
                    &chunk_buffer,
                    nullptr,
                    strides_in_chunk,
                    &payload_sizes_ptr,
                    nullptr);  //In this example we don't use dynamic user header sizes.
                if (unlikely(status == RMAX_SIGNAL)) {
                    goto end;
                }
            } while (status != RMAX_OK);

            chunk_builder.fill_chunk((uint8_t*)chunk_buffer, payload_sizes_ptr, *data.next_chunk_send_time_ns);
            do {
                uint64_t timeout = (uint64_t)*data.next_chunk_send_time_ns;
                if (unlikely(timeout + 600 < get_tai_time_ns())) {
                    timeout = 0;
                } else {
                    /*
                    * When timer handler callback is not used we have a mismatch between
                    * media_sender clock (TAI) and rivermax clock (UTC).
                    * To fix this we are calling to align_to_rmax_time function to convert
                    * @time from TAI to UTC
                    */
                    timeout = align_to_rmax_time(timeout);
                }
                status = rmax_out_commit(stream_id, timeout, c_flags);
                if (status == RMAX_ERR_HW_COMPLETION_ISSUE) {
                    std::cout << "got completion issue exiting" << std::endl;
                    goto end;
                }
                if (unlikely(status == RMAX_SIGNAL)) {
                    goto end;
                }
            } while (status != RMAX_OK);
        }

        if (!loop) {
            break;
        }


        if (!disable_synchronization) {
            std::unique_lock<std::mutex> lock(*data.sync_lock);
            data.eof_stream_counter->fetch_add(1);
            data.eof_cv->notify_one();
            data.sync_cv->wait(lock);
            cst_data time_calculation_data(data.video_width, data.video_height,
                                       data.fps, data.video_type, data.video_pix_format, data.video_sample_rate);
            calculate_stream_time(eMediaType_t::ancillary, data.next_chunk_send_time_ns,
                                      time_calculation_data, nullptr);
        }
    }
end:
    std::cout << "Done sending ancillary" << std::endl;
    rmax_out_cancel_unsent_chunks(stream_id);
    do {
        std::this_thread::sleep_for(milliseconds{300});
        status = rmax_out_destroy_stream(stream_id);
    } while (status == RMAX_ERR_BUSY);
    // Notify all other waiting threads that current thread is finished
    data.notify_all_cv();
}

void rivermax_audio_sender(AudioRmaxData data)
{
    if (unlikely(!run_threads)) {
        return;
    }
    rt_set_thread_affinity(data.affinity_mask_get());
    rt_set_thread_priority(RMAX_THREAD_PRIORITY_TIME_CRITICAL);

    const size_t num_of_av_packet_in_chunk = 3;
    const size_t bit_depth_in_bytes = data.bit_depth_in_bytes;  //3 -> 24-bit, 4 -> 32-bit
    const size_t num_of_samples_in_av_packet = 1024;
    const size_t num_of_chunks = 50;
    const size_t samples_in_stride = data.sample_rate / (microseconds{ seconds{ 1 } } / microseconds{ data.ptime_usec });
    const size_t strides_in_chunk = (num_of_samples_in_av_packet * num_of_av_packet_in_chunk) / samples_in_stride;
    const uint16_t payload_size =
        (uint16_t)(bit_depth_in_bytes * data.channels * samples_in_stride);
    const uint16_t payload_size_with_rtp = payload_size + RTP_HEADER_SIZE;
    const uint16_t packet_stride_size = river_align_up_pow2(payload_size_with_rtp, get_cache_line_size()); // align to cache line
    std::vector<uint16_t> sizes;

    sizes.resize(strides_in_chunk * num_of_chunks, payload_size_with_rtp);

    rmax_mem_block block;
    memset(&block, 0, sizeof(block));

    block.chunks_num = num_of_chunks;
    block.data_size_arr = sizes.data();

    rmax_buffer_attr buffer_attr;
    memset(&buffer_attr, 0, sizeof(buffer_attr));

    rmax_qos_attr qos = { data.dscp, 0 };

    buffer_attr.chunk_size_in_strides = strides_in_chunk;
    buffer_attr.mem_block_array = &block;
    buffer_attr.mem_block_array_len = 1;
    buffer_attr.data_stride_size = packet_stride_size;
    buffer_attr.app_hdr_stride_size = 0;

    std::ifstream is(data.sdp_path);
    std::string sdp_cont((std::istreambuf_iterator<char>(is)),
        std::istreambuf_iterator<char>());

    rmax_stream_id stream_id;
    rmax_status_t status = rmax_out_create_stream(const_cast<char*>(sdp_cont.c_str()),
        &buffer_attr, &qos, 0, 1 /*audio stream index*/, &stream_id);
    if (status != RMAX_OK) {
        std::cerr << "failed creating audio output stream, got status:" << status;
        run_threads = false;
        data.notify_all_cv();
        return;
    }
    std::cout<<"audio stream created with ID "<<stream_id<<std::endl;
    RtpAudioHeaderBuilder chunk_builder = RtpAudioHeaderBuilder(
        payload_size
        , data.payload_type
        , strides_in_chunk
        , data.sample_rate
        , data.ptime_usec
        , packet_stride_size
        , samples_in_stride
        , data.channels
        , num_of_samples_in_av_packet
        , bit_depth_in_bytes
        , data.timestamp_tick);

    EventMgr event_mgr;
    if (!disable_wait_for_event && !event_mgr.init(stream_id)) {
        run_threads = false;
        data.notify_all_cv();
        return;
    }

    uint64_t frame_send_time_ns = (uint64_t)nanoseconds{milliseconds{(uint64_t)strides_in_chunk}}.count();
    go_to_sleep((uint64_t)*data.next_chunk_send_time_ns, (uint64_t)nanoseconds{seconds{1}}.count());

    std::cout << "Audio sender is on!" << std::endl;

start:
    bool first_loop = true;
    uint32_t arr_index = 0;
    const uint32_t number_of_arrs = 2;
    std::shared_ptr<AVPacket> sptr_av_packet_arr[number_of_arrs][num_of_av_packet_in_chunk];
    while (likely(!exit_app()) && run_threads) {
        rmax_commit_flags_t c_flags{};
        for (size_t i = 0; i < num_of_av_packet_in_chunk; ++i) {
            std::shared_ptr<queued_data> qdata;
            data.send_cb->try_dequeue(qdata);
            if (!qdata) {
                std::cout << "Audio sender is waiting" << std::endl;
                std::unique_lock<std::mutex> lock(*data.send_lock);
                data.send_cv->wait(lock);
                data.send_cb->try_dequeue(qdata);
            }

            data.send_cv->notify_one();
            if (qdata->queued_data_info == queued_data::e_qdi_eof) {
                c_flags = RMAX_PAUSE_AFTER_COMMIT;
                break;
            }

            uint32_t next_arr_index = (arr_index + 1) % number_of_arrs;
            sptr_av_packet_arr[next_arr_index][i] = qdata->packet;
        }

        if (first_loop) {
            first_loop = false;
            arr_index = (arr_index + 1) % number_of_arrs;
            continue;
        }

        //Build chunk
        void *chunk_buffer = nullptr;
        do {
            status = rmax_out_get_next_chunk(stream_id, &chunk_buffer, nullptr);
            if (unlikely(status == RMAX_SIGNAL)) {
                goto end;
            }
        } while (status != RMAX_OK);

        chunk_builder.fill_chunk((uint8_t*)chunk_buffer, sptr_av_packet_arr[arr_index]);
        do {
            /*
            * When timer handler callback is not used we have a mismatch between
            * media_sender clock (TAI) and rivermax clock (UTC).
            * To fix this we are calling to align_to_rmax_time function to convert
            * @time from TAI to UTC
            */
            status = rmax_out_commit(stream_id, align_to_rmax_time((uint64_t)*data.next_chunk_send_time_ns), c_flags);
            if (status == RMAX_ERR_NO_FREE_CHUNK && !disable_wait_for_event) {
                event_mgr.request_notification(stream_id);
            }
            if (status == RMAX_ERR_HW_COMPLETION_ISSUE) {
                std::cout << "got completion issue exiting" << std::endl;
                goto end;
            }
            if (unlikely(status == RMAX_SIGNAL)) {
                goto end;
            }
        } while (status != RMAX_OK);
        *data.next_chunk_send_time_ns += (double)frame_send_time_ns;
        arr_index = (arr_index + 1) % number_of_arrs;

        if (c_flags & RMAX_PAUSE_AFTER_COMMIT) {
            if (!loop) {
                goto end;
            }

            if (!disable_synchronization) {
                std::unique_lock<std::mutex> lock(*data.sync_lock);
                data.eof_stream_counter->fetch_add(1);
                data.eof_cv->notify_one();
                data.sync_cv->wait(lock);
                cst_data time_calculation_data(data.ptime_usec, data.sample_rate, data.video_fps);
                calculate_stream_time(audio, data.next_chunk_send_time_ns, time_calculation_data, &chunk_builder.m_timestamp_tick);
            }
            goto start;
        }
    }

end:
    std::cout << "done sending audio" << std::endl;
    rmax_out_cancel_unsent_chunks(stream_id);
    do {
        std::this_thread::sleep_for(milliseconds{300});
        status = rmax_out_destroy_stream(stream_id);
    } while (status == RMAX_ERR_BUSY);
    // Notify all other waiting threads that current thread is finished
    data.notify_all_cv();
}

void rivermax_video_sender(VideoRmaxData data)
{
    if (unlikely(!run_threads)) {
        return;
    }
    // can be any number
    int strides_in_chunk = 256;
    int packets_in_frame_or_field = 0;
    uint32_t chunks_num_per_frame_or_field;
    uint16_t px_group_byte_size;
    rt_set_thread_affinity(data.affinity_mask_get());
    rt_set_thread_priority(RMAX_THREAD_PRIORITY_TIME_CRITICAL);
    /*
     * calculate packet sizes using pixel format H & W
     * Pixel format must be either:
     * 1. AV_PIX_FMT_YUV422P
     * 2. AV_PIX_FMT_UYVY422
     * 3. AV_PIX_FMT_YUV422P10LE
     * 4. AV_PIX_FMT_UYVY422
     * bit supported are 10 bit and 8 bit where AV_PIX_FMT_YUV422P10LE
     * is 10 bit and the rest are 8 bit.
     * Note: we only support 4:2:2
     */
    // according to 2110-10
    const uint16_t max_packet_size = data.max_payload_size;
    const uint16_t max_payload_size = max_packet_size - IPV4_HDR_SIZE - UDP_HDR_SIZE;
    // align to cache line
    const uint16_t packet_stride = river_align_up_pow2(max_payload_size, get_cache_line_size());
    const uint16_t user_header_size = sizeof(rtp_header) + sizeof(uint16_t) + sizeof(srd_header);
    // 10 bit
    if (data.pix_format == AVPixelFormat::AV_PIX_FMT_YUV422P10LE) {
        px_group_byte_size = BYTES_IN_422_10B_GRP;
    } else {// must be 8 bits
        px_group_byte_size = BYTES_IN_422_8B_GRP;
    }
    uint16_t px_groups_in_line = data.width / PX_IN_422_GRP;
    uint16_t height = data.height;
    if (data.video_type != VIDEO_TYPE::PROGRESSIVE) {
        height /= 2;
    }

    int px_groups_left_in_frame_field = px_groups_in_line * height;

    std::vector<uint16_t> sizes;
    uint16_t tmp_px_groups_in_line = px_groups_in_line;
    while (px_groups_left_in_frame_field > 0) {
        int used_pgroups = 0;
        uint16_t payload_size = max_payload_size - user_header_size;
        if (tmp_px_groups_in_line <= (payload_size - sizeof(srd_header)) / px_group_byte_size) {
            if ((int)px_groups_left_in_frame_field <
                (int)((payload_size - sizeof(srd_header)) / px_group_byte_size)) {
                // add padding to last packet
                used_pgroups = px_groups_left_in_frame_field;
                if (data.allow_padding) {
                    payload_size = sizes[sizes.size() - 1];
                } else {
                    payload_size = used_pgroups * px_group_byte_size + user_header_size;
                }
            } else {
                // 2 SRDs
                payload_size -= sizeof(srd_header);
                used_pgroups = payload_size / px_group_byte_size;
                payload_size = (uint16_t)(used_pgroups * px_group_byte_size + user_header_size +
                    sizeof(srd_header));
                tmp_px_groups_in_line = px_groups_in_line - (used_pgroups - tmp_px_groups_in_line);
            }
        } else {
            // 1SRDS
            used_pgroups = payload_size / px_group_byte_size;
            payload_size = used_pgroups * px_group_byte_size + user_header_size;
            tmp_px_groups_in_line -= used_pgroups;
        }
        if (!tmp_px_groups_in_line) {
            tmp_px_groups_in_line = px_groups_in_line;
        }
        px_groups_left_in_frame_field -= used_pgroups;
        sizes.push_back((uint16_t)payload_size);
    }

    packets_in_frame_or_field = (int)sizes.size();

    // can be any number
    strides_in_chunk = 256;
    chunks_num_per_frame_or_field = (uint32_t)std::ceil((double)packets_in_frame_or_field / strides_in_chunk);
    // sizes must have zeroes at the end to complete to this size
    sizes.resize(chunks_num_per_frame_or_field * strides_in_chunk, 0);

    rmax_buffer_attr buffer_attr;
    memset(&buffer_attr, 0, sizeof(buffer_attr));
    rmax_qos_attr qos;
    memset(&qos, 0, sizeof(qos));

    // can be any number bigger then 1
    int mem_block_size = (int)(data.fps / 2);
    double frame_field_time_interval_ns = ((double)nanoseconds{seconds{1}}.count())/data.fps;
    int packets_in_frame = packets_in_frame_or_field;
    if (data.video_type != VIDEO_TYPE::PROGRESSIVE) {
        frame_field_time_interval_ns /= 2;
        mem_block_size *= 2;
        packets_in_frame *= 2;
    }

    std::vector<rmax_mem_block> block(mem_block_size);

    for (int i = 0; i < mem_block_size; i++) {
        block[i].chunks_num = chunks_num_per_frame_or_field;
        block[i].data_size_arr = sizes.data();
    }
    buffer_attr.chunk_size_in_strides = strides_in_chunk;
    buffer_attr.mem_block_array = block.data();
    buffer_attr.mem_block_array_len = mem_block_size;
    buffer_attr.data_stride_size = packet_stride;

    std::ifstream is(data.sdp_path);
    std::string sdp_cont((std::istreambuf_iterator<char>(is)),
        std::istreambuf_iterator<char>());
    rmax_stream_id stream_id;
    rmax_status_t status = rmax_out_create_stream(const_cast<char*>(sdp_cont.c_str()),
        &buffer_attr, &qos,
        packets_in_frame, 0 /* video stream index */, &stream_id);
    if (status != RMAX_OK) {
        std::cerr << "failed creating video output stream, got status:" << status;
        run_threads = false;
        data.notify_all_cv();
        return;
    }
    std::cout<<"video stream created with ID "<<stream_id<<std::endl;
    uint16_t bit_depth = data.pix_format == AVPixelFormat::AV_PIX_FMT_YUV422P10LE ? 10: 8;
    RtpVideoHeaderBuilder frame_field_builder = RtpVideoHeaderBuilder(height,
                                                                data.width,
                                                                packets_in_frame_or_field,
                                                                bit_depth,
                                                                px_group_byte_size,
                                                                data.fps,
                                                                data.payload_type,
                                                                data.timestamp_tick,
                                                                sizes,
                                                                data.video_type,
                                                                data.pix_format);

    EventMgr event_mgr;
    if (!disable_wait_for_event && !event_mgr.init(stream_id)) {
        run_threads = false;
        data.notify_all_cv();
        return;
    }
    rmax_commit_flags_t c_flags{};

    go_to_sleep((uint64_t)*data.next_frame_field_send_time_ns, (uint64_t)nanoseconds{seconds{1}}.count());
    std::cout << "Video sender is on!" << std::endl;

start:
    uint64_t sent_frames_or_fields = 0;
    double start_send_time_ns = *data.next_frame_field_send_time_ns;
    while (likely(!exit_app()) && run_threads) {
        std::shared_ptr<queued_data> qdata;
        data.send_cb->try_dequeue(qdata);

        if (!qdata) {
            std::cout << "Video sender is waiting" << std::endl;
            std::unique_lock<std::mutex> lock(*data.send_lock);
            data.send_cv->wait(lock);
            data.send_cb->try_dequeue(qdata);
        }

        if (qdata->queued_data_info == queued_data::e_qdi_eof) {
            if (!loop) {
                goto end;
            }

            if (!disable_synchronization) {
                std::unique_lock<std::mutex> lock(*data.sync_lock);
                data.eof_stream_counter->fetch_add(1);
                data.eof_cv->notify_one();
                data.sync_cv->wait(lock);
                cst_data time_calculation_data(data.width, data.height, data.fps, data.video_type, data.pix_format, data.sample_rate);
                calculate_stream_time(video, data.next_frame_field_send_time_ns, time_calculation_data, &frame_field_builder.m_timestamp_tick);
            }
            goto start;
        }

        std::shared_ptr<AVFrame> av_frame = qdata->frame;
        data.send_cv->notify_one();

        const uint32_t loops_per_av_fram =( data.video_type != VIDEO_TYPE::PROGRESSIVE ? 2 : 1);
        for (uint32_t loop_per_av_fram = 0; loop_per_av_fram < loops_per_av_fram; ++loop_per_av_fram ) {
            SendData sd{ 0, px_groups_in_line * height, px_groups_in_line };
            frame_field_builder.m_Y = &av_frame->data[0][0];
            frame_field_builder.m_Cb = &av_frame->data[1][0];
            frame_field_builder.m_Cr = &av_frame->data[2][0];
            frame_field_builder.set_counters();
            for (uint32_t chunk = 0; chunk < chunks_num_per_frame_or_field && sd.packet_counter < packets_in_frame_or_field; ++chunk) {
                uint8_t *chunk_buffer;
                do {
                    status = rmax_out_get_next_chunk(stream_id, (void**)&chunk_buffer, nullptr);
                    if (status == RMAX_ERR_NO_FREE_CHUNK && !disable_wait_for_event) {
                        event_mgr.request_notification(stream_id);
                    }
                    if (unlikely(status == RMAX_SIGNAL)) {
                        goto end;
                    }
                } while (status != RMAX_OK);
                // fill chunk
                for (int stride = 0; stride < strides_in_chunk &&
                     sd.packet_counter < packets_in_frame_or_field; ++stride, ++sd.packet_counter) {
                    frame_field_builder.fill_packet(chunk_buffer, sd, av_frame.get());
                    chunk_buffer += packet_stride;
                }

                do {
                    uint64_t timeout = 0;
                    // assume gap mode!
                    if (chunk == 0) {
                        timeout = (uint64_t)*data.next_frame_field_send_time_ns;
                        // verify windows is at least 600 nanos away
                        if (unlikely(timeout + 600 < get_tai_time_ns())) {
                            timeout = 0;
                        } else {
                            /*
                            * When timer handler callback is not used we have a mismatch between
                            * media_sender clock (TAI) and rivermax clock (UTC).
                            * To fix this we are calling to align_to_rmax_time function to convert
                            * @time from TAI to UTC
                            */
                            timeout = align_to_rmax_time(timeout);
                        }
                    }
                    if (timeout) {
    #ifdef DEBUG
                        std::cout << "calling with timeout " << timeout << " now " <<
                            get_tai_time_ns() << std::endl;
    #endif
                    }
                    status = rmax_out_commit(stream_id, timeout, c_flags);
                    if (status == RMAX_ERR_HW_COMPLETION_ISSUE) {
                        std::cout << "got completion issue exiting" << std::endl;
                        goto end;
                    }
                    if (unlikely(status == RMAX_SIGNAL)) {
                        goto end;
                    }
                } while (status != RMAX_OK);
            }

            if (data.video_type != VIDEO_TYPE::PROGRESSIVE) {
                frame_field_builder.m_field = !frame_field_builder.m_field;
            }
            ++sent_frames_or_fields;
            *data.next_frame_field_send_time_ns += frame_field_time_interval_ns;
        }

        *data.next_frame_field_send_time_ns = (start_send_time_ns + sent_frames_or_fields * frame_field_time_interval_ns);
    }
end:
    std::cout << "done sending video" << std::endl;
    rmax_out_cancel_unsent_chunks(stream_id);
    do {
        std::this_thread::sleep_for(milliseconds{300});
        status = rmax_out_destroy_stream(stream_id);
    } while (status == RMAX_ERR_BUSY);
    // Notify all other waiting threads that current thread is finished
    data.notify_all_cv();
}

void scale_video(ScaleDataVideo scale_data)
{
    rt_set_thread_affinity(scale_data.affinity_mask_get());
    rt_set_thread_priority(RMAX_THREAD_PRIORITY_TIME_CRITICAL);

    std::unique_ptr<SwsContext, std::function<void(SwsContext*)>> swsContext{
        sws_getContext(scale_data.rmax_data.width, scale_data.rmax_data.height,
                       scale_data.rmax_data.pix_format, scale_data.rmax_data.width,
                       scale_data.rmax_data.height,
                       AV_PIX_FMT_UYVY422, SWS_BILINEAR, nullptr, nullptr, nullptr),
                            [](SwsContext* p) { sws_freeContext(p); } };
    while (likely(!exit_app()) && run_threads) {
        // Video
        std::shared_ptr<queued_data> qdata;
        scale_data.conv_cb->try_dequeue(qdata);
        if (!qdata) {
            std::unique_lock<std::mutex> lock(*scale_data.conv_lock);
            scale_data.conv_cv->wait(lock);
            scale_data.conv_cb->try_dequeue(qdata);
        }

        if (qdata->queued_data_info == queued_data::e_qdi_eof) {
            if (!scale_data.rmax_data.send_cb->try_enqueue(std::move(qdata))) {
                std::unique_lock<std::mutex> lock(*scale_data.rmax_data.send_lock);
                scale_data.rmax_data.send_cv->wait(lock);
                scale_data.rmax_data.send_cb->enqueue(std::move(qdata));
            }
            scale_data.rmax_data.send_cv->notify_all();
            if (!loop) {
                break;
            }
            continue;
        }

        scale_data.conv_cv->notify_all();
        std::shared_ptr<queued_data> dst_qdata = std::make_shared<queued_data>();
        if (qdata->frame->format != AVPixelFormat::AV_PIX_FMT_YUV422P &&
            qdata->frame->format != AVPixelFormat::AV_PIX_FMT_UYVY422 &&
            qdata->frame->format != AVPixelFormat::AV_PIX_FMT_YUV422P10LE) {
            std::shared_ptr<AVFrame> dstframe{ av_frame_alloc(), AVFrameDeleter};
            dstframe->format = AV_PIX_FMT_UYVY422;
            dstframe->width = qdata->frame->width;
            dstframe->height = qdata->frame->height;

            int ret = av_frame_get_buffer(dstframe.get(), 64);
            if (ret < 0)
            {
                throw std::bad_alloc();
            }
            ret = sws_scale(swsContext.get(), qdata->frame->data, qdata->frame->linesize, 0,
                            qdata->frame->height, dstframe->data, dstframe->linesize);

            if (ret < 0)
            {
                throw std::runtime_error("failed scaling frame to AV_PIX_FMT_UYVY422");
            }
            dst_qdata->frame = dstframe;
            if (!scale_data.rmax_data.send_cb->try_enqueue(std::move(dst_qdata))) {
                std::unique_lock<std::mutex> lock(*scale_data.rmax_data.send_lock);
                scale_data.rmax_data.send_cv->wait(lock);
                scale_data.rmax_data.send_cb->enqueue(std::move(dst_qdata));
            }
        } else {
            dst_qdata->frame = qdata->frame;
            if (!scale_data.rmax_data.send_cb->try_enqueue(std::move(dst_qdata))) {
                std::unique_lock<std::mutex> lock(*scale_data.rmax_data.send_lock);
                scale_data.rmax_data.send_cv->wait(lock);
                scale_data.rmax_data.send_cb->enqueue(std::move(dst_qdata));
            }
        }
        scale_data.rmax_data.send_cv->notify_all();
    }
    // Notify all other waiting threads that current thread is finished
    scale_data.notify_all_cv();
}

void encode_audio(AudioEncodeData audio_encode_data)
{
    rt_set_thread_affinity(audio_encode_data.affinity_mask_get());
    rt_set_thread_priority(RMAX_THREAD_PRIORITY_TIME_CRITICAL);

    auto *p_audio_codec_pcm = avcodec_find_encoder(AV_CODEC_ID_PCM_S24BE);
    if (!p_audio_codec_pcm) {
        std::cerr << "failed find PCM_S24BE encoder";
        return;
    }

    //Process Audio
    auto *p_audio_codec_context_pcm = avcodec_alloc_context3(p_audio_codec_pcm);
    std::unique_ptr<AVCodecContext, av_deleter> ctx_guard(p_audio_codec_context_pcm);
    if (!p_audio_codec_pcm) {
        std::cerr << "failed load PCM_S24BE encoder context";
        return;
    }

    p_audio_codec_context_pcm->bit_rate = audio_encode_data.rmax_data.bit_rate;
    p_audio_codec_context_pcm->sample_rate = 48000;
    p_audio_codec_context_pcm->channel_layout = audio_encode_data.rmax_data.channel_layout;
    p_audio_codec_context_pcm->channels = audio_encode_data.rmax_data.channels;
    p_audio_codec_context_pcm->sample_fmt = AV_SAMPLE_FMT_S32;

    if (avcodec_open2(p_audio_codec_context_pcm, p_audio_codec_pcm, nullptr) < 0) {
        std::cerr << "failed to open audio codec through avcodec_open2";
        return;
    }

    SwrContext *swr = nullptr;
    while (likely(!exit_app()) && run_threads) {
        std::shared_ptr<queued_data> qdata;
        audio_encode_data.conv_cb->try_dequeue(qdata);
        if (!qdata) {
            std::unique_lock<std::mutex> lock(*audio_encode_data.conv_lock);
            audio_encode_data.conv_cv->wait(lock);
            audio_encode_data.conv_cb->try_dequeue(qdata);
        }

        if (qdata->queued_data_info == queued_data::e_qdi_eof) {
            if (!audio_encode_data.rmax_data.send_cb->try_enqueue(std::move(qdata))) {
                std::unique_lock<std::mutex> lock(*audio_encode_data.rmax_data.send_lock);
                audio_encode_data.rmax_data.send_cv->wait(lock);
                audio_encode_data.rmax_data.send_cb->enqueue(std::move(qdata));
            }
            audio_encode_data.rmax_data.send_cv->notify_all();
            if (!loop) {
                break;
            }
            continue;
        }

        audio_encode_data.conv_cv->notify_all();

        if (48000 != qdata->frame->sample_rate || AV_SAMPLE_FMT_S32 != qdata->frame->format) {
            std::shared_ptr<AVFrame> new_av_frame{ av_frame_alloc(), AVFrameDeleter };
            if (!swr) {
                swr = swr_alloc_set_opts(nullptr,  // we're allocating a new context
                    qdata->frame->channel_layout, // out_ch_layout
                    AV_SAMPLE_FMT_S32,  // out_sample_fmt
                    48000,  // out_sample_rate
                    qdata->frame->channel_layout,  // in_ch_layout
                    (AVSampleFormat)qdata->frame->format,  // in_sample_fmt
                    qdata->frame->sample_rate,  // in_sample_rate
                    0,  // log_offset
                    nullptr);  // log_ctx
            }

            new_av_frame->format = AVSampleFormat::AV_SAMPLE_FMT_S32;
            new_av_frame->channel_layout = qdata->frame->channel_layout;
            new_av_frame->sample_rate = qdata->frame->sample_rate;
            new_av_frame->pkt_duration = qdata->frame->pkt_duration;

            if (swr_convert_frame(swr, new_av_frame.get(), qdata->frame.get())) {
                std::cerr << "failed to convert audio frame with avresample_convert_frame" << std::endl;
                return;
            }

            qdata->frame = new_av_frame;
        }

        std::shared_ptr<AVPacket> pPacket{ new AVPacket, AVPacketDeleter };
        av_init_packet(pPacket.get());
        pPacket->data = nullptr; // packet data will be allocated by the encoder
        pPacket->size = 0;

        int ret = 0;
        while (likely(!exit_app()) && run_threads) {
            ret = avcodec_send_frame(p_audio_codec_context_pcm, qdata->frame.get());
            if (ret == AVERROR(EAGAIN)) {
                while (avcodec_receive_packet(p_audio_codec_context_pcm, pPacket.get()))
                    continue;
            } else if (ret) {
                std::cout << "Error while sending an audio packet to the encoder: " << ret << std::endl;;
                return;
            }

            break;
        }

        ret = avcodec_receive_packet(p_audio_codec_context_pcm, pPacket.get());
        if (ret) {
            std::cout << "Error while receiving an audio packet from the encoder: " << ret << std::endl;;
            return;
        }

        std::shared_ptr<queued_data> q_data = std::make_shared<queued_data>();
        q_data->packet = pPacket;
        if (!audio_encode_data.rmax_data.send_cb->try_enqueue(std::move(q_data))) {
            std::unique_lock<std::mutex> lock(*audio_encode_data.rmax_data.send_lock);
            audio_encode_data.rmax_data.send_cv->wait(lock);
            audio_encode_data.rmax_data.send_cb->try_enqueue(std::move(q_data));
        }
        audio_encode_data.rmax_data.send_cv->notify_all();
    }

    if (swr) {
        swr_free(&swr);
    }
    // Notify all other waiting threads that current thread is finished
    audio_encode_data.notity_all_cv();
}

template<typename T>
void read_stream(T rd)
{
    AVCodecContext *p_codec_context = avcodec_alloc_context3(rd.p_codec);
    std::unique_ptr<AVCodecContext, av_deleter> ctx_guard(p_codec_context);
    if (!p_codec_context) {
        std::cerr << "failed to allocated memory for " << rd.stream_name << " AVCodecContext\n";
        return;
    }
    p_codec_context->thread_count = rd.ffmpeg_thread_count;
    p_codec_context->active_thread_type = FF_THREAD_SLICE;
    if (avcodec_parameters_to_context(p_codec_context, rd.p_codec_parameters) < 0) {
        std::cerr << "failed to copy " << rd.stream_name << " codec params to codec context\n";
        return;
    }
    p_codec_context->active_thread_type = FF_THREAD_SLICE;
    if (avcodec_open2(p_codec_context, rd.p_codec, nullptr) < 0) {
        std::cerr << "failed to open " << rd.stream_name << " codec through avcodec_open2\n";
        return;
    }

    // Set reader thread affinity after initializing ffmpeg context, otherwise all
    // @ref rd.ffmpeg_thread_count ffmpeg threads will inherit the same core as this reader thread.
    // It will let ffmpeg threads to be set by the OS based on free available cores.
    rt_set_thread_affinity(rd.affinity_mask_get());
    rt_set_thread_priority(RMAX_THREAD_PRIORITY_TIME_CRITICAL);

    uint64_t frames = 0;
    while (likely(!exit_app()) && run_threads) {
        std::unique_ptr<AVPacket, std::function<void(AVPacket*)>> packet{
                        new AVPacket,
                        [](AVPacket* p) { av_packet_unref(p); delete p; } };
        std::shared_ptr<queued_data> qdata = std::make_shared<queued_data>();

        av_init_packet(packet.get());
        int response = av_read_frame(*rd.p_format_context.get(), packet.get());
        if (AVERROR_EOF == response) {
            std::cout << "EOF while reading " << rd.stream_name << " frame (" << frames << ")." << std::endl;
            qdata->queued_data_info = queued_data::e_qdi_eof;
            if (!rd.conv_cb->try_enqueue(std::move(qdata))) {
                std::unique_lock<std::mutex> lock(*rd.conv_lock);
                rd.conv_cv->wait(lock);
                rd.conv_cb->enqueue(std::move(qdata));
            }
            rd.conv_cv->notify_all();
            if (loop) {
                av_seek_frame(*rd.p_format_context.get(), rd.stream_index, 0, 0);
                avcodec_flush_buffers(p_codec_context);
                frames = 0;
                if (std::is_same<T, VideoReaderData>::value) { /* XXX */
                    avformat_close_input(rd.p_format_context.get());
                    response = avformat_open_input(rd.p_format_context.get(), rd.file_path.c_str(), nullptr, nullptr);
                    if (response != 0) {
                        std::cerr << "Error while open video file" << std::endl;
                    }
                }
                continue;
            }
            return;
        } else if (response < 0) {
            std::cout << "Error while reading " << rd.stream_name << " frame: " << response << std::endl;
            return;
        }
        if (packet->stream_index != rd.stream_index) {
            continue;
        }
        // send packet to decoder
        response = avcodec_send_packet(p_codec_context, packet.get());
        if (response < 0) {
            std::cout << "Error while sending a " << rd.stream_name << " packet to the decoder: " << response << std::endl;
            continue;
        }
        while (response >= 0 && run_threads) {
            std::shared_ptr<AVFrame> pFrame{ av_frame_alloc(), AVFrameDeleter };
            response = avcodec_receive_frame(p_codec_context, pFrame.get());
            if (response == AVERROR(EAGAIN)) {
                continue;
            }
            if (response == AVERROR_EOF) {
                std::cerr << "got to last " << rd.stream_name << " frame after " << frames << std::endl;
                if (loop) {
                    qdata->queued_data_info = queued_data::e_qdi_eof;
                    if (!rd.conv_cb->try_enqueue(std::move(qdata))) {
                        std::unique_lock<std::mutex> lock(*rd.conv_lock);
                        rd.conv_cv->wait(lock);
                        rd.conv_cb->enqueue(std::move(qdata));
                    }
                    rd.conv_cv->notify_all();
                    av_seek_frame(*rd.p_format_context.get(), rd.stream_index, 0, 0);
                    avcodec_flush_buffers(p_codec_context);
                    frames = 0;
                    if (std::is_same<T, VideoReaderData>::value) { /* XXX */
                        avformat_close_input(rd.p_format_context.get());
                        response = avformat_open_input(rd.p_format_context.get(), rd.file_path.c_str(), nullptr, nullptr);
                        if (response != 0) {
                            std::cerr << "Error while open video file" << std::endl;
                        }
                    }
                    break;
                } else {
                    std::shared_ptr<queued_data> tmp_frmae;
                    while (!rd.conv_cb->try_dequeue(tmp_frmae)) {
                        std::unique_lock<std::mutex> lock(*rd.conv_lock);
                        rd.conv_cv->wait(lock);
                    }
                    std::cout << "done reading " << rd.stream_name << " file" << std::endl;
                    return;
                }
            } else if (response < 0) {
                std::cerr << "Error while receiving a " << rd.stream_name << " frame from the decoder: " << response << std::endl;
                return;
            }

            if (response >= 0) {
                ++frames;
                qdata->frame = pFrame;
                if (!rd.conv_cb->try_enqueue(std::move(qdata))) {
                    std::unique_lock<std::mutex> lock(*rd.conv_lock);
                    rd.conv_cv->wait(lock);
                    rd.conv_cb->enqueue(std::move(qdata));
                }
                rd.conv_cv->notify_all();
            }
        }
    }
    // Notify all other waiting threads that current thread is finished
    rd.notify_all_cv();
}

int get_context(const char *file_path, AVFormatContext *&p_format_context)
{
    // AVFormatContext holds the header information from the format (Container)
    // Allocating memory for this component
    p_format_context = avformat_alloc_context();
    if (!p_format_context) {
        std::cerr << "ERROR could not allocate memory for Format Context." << std::endl;
        return -1;
    }

    // Open the file and read its header. The codecs are not opened.
    if (avformat_open_input(&p_format_context, file_path, nullptr, nullptr) != 0) {
        avformat_free_context(p_format_context);
        std::cerr << "ERROR could not open the file: " << file_path << std::endl;
        return -1;
    }

    // Read Packets from the Format to get stream information
    if (avformat_find_stream_info(p_format_context, nullptr) < 0) {
        avformat_close_input(&p_format_context);
        avformat_free_context(p_format_context);
        std::cerr << "ERROR could not get the stream info" << std::endl;
        return -1;
    }

    return 0;
}

int video_process_file(const char *file_path, VideoRmaxData &rmax_data, VideoReaderData& video_reader_data)
{
    //Open file
    AVFormatContext *p_video_format_context = nullptr;

    if (get_context(file_path, p_video_format_context)) {
        std::cerr << "ERROR could not get video context." << std::endl;
        return -1;
    }

    std::cout << std::endl;
    std::cout << "File information: "<< file_path << std::endl;
    av_dump_format(p_video_format_context, 0, file_path, 0);
    std::cout << std::endl;

    const AVCodec *p_video_codec = nullptr;
    AVCodecParameters *p_video_codec_parameters = nullptr;
    int video_stream_index = -1;
    for (uint32_t i = 0; i < p_video_format_context->nb_streams && !exit_app(); i++) {
        AVCodecParameters *pLocalCodecParameters = p_video_format_context->streams[i]->codecpar;
        if (pLocalCodecParameters->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = i;
            p_video_codec = avcodec_find_decoder(pLocalCodecParameters->codec_id);;
            p_video_codec_parameters = pLocalCodecParameters;
            break;
        }
    }

    if (video_stream_index >= 0) {
        //Init rmax_data
        rmax_data.width = p_video_codec_parameters->width;
        rmax_data.height = p_video_codec_parameters->height;
        rmax_data.fps = av_q2d(p_video_format_context->streams[video_stream_index]->r_frame_rate);
        rmax_data.pix_format = (AVPixelFormat)p_video_codec_parameters->format;

        //Init reader_data
        video_reader_data.stream_index = video_stream_index;
        video_reader_data.p_format_context = std::make_shared<AVFormatContext*>(std::move(p_video_format_context));
        video_reader_data.p_codec = p_video_codec;
        video_reader_data.p_codec_parameters = p_video_codec_parameters;
        video_reader_data.file_path.assign(file_path);

        int64_t duration_in_secounds = p_video_format_context->duration/AV_TIME_BASE;
        rmax_data.duration = duration_in_secounds;
        std::cout << "Video"
            << "\n\t codec: " << p_video_codec->long_name
            << "\n\t video pix format is: "
            << av_get_pix_fmt_name((AVPixelFormat)p_video_codec_parameters->format)
            << "\n\t height: " << p_video_codec_parameters->height
            << "\n\t width: " << p_video_codec_parameters->width
            << "\n\t fps: " << rmax_data.fps
            << "\n\t stream index: " << video_stream_index
            << "\n\t duration: "
            << "0" << duration_in_secounds/(60*60)
            << ":" << (duration_in_secounds/60)
            << ":" << (duration_in_secounds%60)
            << "." << (p_video_format_context->duration%AV_TIME_BASE)
            << std::endl << std::endl;
    } else {
        avformat_close_input(&p_video_format_context);
        std::cerr << "Failed finding video valid stream" << std::endl;
        return -1;
    }

    return 0;
}

bool audio_process_file(const char *file_path, AudioRmaxData &audio_rmax_data,
                       AudioReaderData& audio_reader_data, MediaData &media_data)
{
    //Open file
    AVFormatContext *p_audio_format_context = nullptr;

    if (get_context(file_path, p_audio_format_context)) {
        std::cerr << "ERROR could not get audio context." << std::endl;
        return false;
    }

    const AVCodec *p_audio_codec = nullptr;
    AVCodecParameters *p_audio_codec_parameters = nullptr;
    int audio_stream_index = -1;
    for (uint32_t i = 0; i < p_audio_format_context->nb_streams && !exit_app(); i++) {
        AVCodecParameters *p_local_codec_parameters = p_audio_format_context->streams[i]->codecpar;
        if (p_local_codec_parameters->codec_type == AVMEDIA_TYPE_AUDIO &&
            p_local_codec_parameters->channels == media_data.channels_num) {
            audio_stream_index = i;
            p_audio_codec = avcodec_find_decoder(p_local_codec_parameters->codec_id);;
            p_audio_codec_parameters = p_local_codec_parameters;
            break;
        }
    }

    if (audio_stream_index >= 0) {
        char ch_layout[64] = {0};
        av_get_channel_layout_string(ch_layout, sizeof(ch_layout), 0, p_audio_codec_parameters->channel_layout);

        int64_t duration_in_secounds = p_audio_format_context->duration/AV_TIME_BASE;

        std::cout << "Audio"
            << "\n\t codac: " << p_audio_codec->long_name
            << "\n\t sample format: " << av_get_sample_fmt_name((AVSampleFormat)p_audio_codec_parameters->format)
            << "\n\t sample_rate: " << p_audio_codec_parameters->sample_rate << " Hz"
            << "\n\t channels: " << p_audio_codec_parameters->channels
            << "\n\t channel_layout: " << ch_layout
            << "\n\t stream index: " << audio_stream_index
            << "\n\t duration: "
            << "0" << duration_in_secounds/(60*60)
            << ":" << (duration_in_secounds/60)
            << ":" << (duration_in_secounds%60)
            << "." << (p_audio_format_context->duration%AV_TIME_BASE)
            << std::endl << std::endl;

        //Init rmax_data
        audio_rmax_data.bit_rate = p_audio_codec_parameters->bit_rate;
        audio_rmax_data.sample_rate = p_audio_codec_parameters->sample_rate;
        audio_rmax_data.channels = p_audio_codec_parameters->channels;
        audio_rmax_data.frame_size = p_audio_codec_parameters->frame_size;
        audio_rmax_data.channel_layout = p_audio_codec_parameters->channel_layout;
        audio_rmax_data.format = (AVSampleFormat)p_audio_codec_parameters->format;

        //Init reader_data
        audio_reader_data.stream_index = audio_stream_index;
        audio_reader_data.p_codec = p_audio_codec;
        audio_reader_data.p_codec_parameters = p_audio_codec_parameters;
        audio_reader_data.p_format_context = std::make_shared<AVFormatContext*>(std::move(p_audio_format_context));
    } else {
        avformat_close_input(&p_audio_format_context);
        std::cerr << "Failed finding Audio valid stream" << std::endl;
        return false;;
    }

    return true;
}

static bool parse_sdp_connection_details(const std::string &sdp, std::string &src_ip)
{
    size_t pos_start;
    size_t pos_end;
    std::string line_str;
    std::vector<std::string> line_vec;

    // parse source and destination ip, dst port parsing is done in video\audio specific function
    if ((pos_start = sdp.find("a=source-filter:")) == std::string::npos) {
        std::cerr << "invalid sdp failed finding a=source connection sections\n";
        return false;
    }

    if ((pos_end = sdp.find_first_of("\r\n", pos_start)) == std::string::npos) {
        std::cerr << "invalid sdp failed finding end of connection section\n";
        return false;
    }

    line_str = sdp.substr(pos_start, pos_end - pos_start);
    line_vec = split_string(line_str, ' ');
    if (line_vec.size() < 5) {
        std::cerr << "invalid sdp failed finding connection details" << line_str << std::endl;;
        return false;
    }
    src_ip = line_vec.back();
    if ((pos_start = sdp.find("c=IN")) == std::string::npos) {
        std::cerr << "invalid sdp failed finding c= connection sections\n";
        return false;
    }

    if ((pos_end = sdp.find_first_of("\r\n", pos_start)) == std::string::npos) {
        std::cerr << "invalid sdp failed finding end of connection section\n";
        return false;
    }

    return true;
}

static void check_sdp_dst_ips(const std::string &sdp, size_t pos_start, size_t &pos_end)
{
    std::string line_str;
    std::vector<std::string> line_vec;
    std::string src_ip, dst_ip;

    if ((pos_start = sdp.find("c=IN")) == std::string::npos) {
        return ;
    }

    if ((pos_end = sdp.find_first_of("\r\n", pos_start)) == std::string::npos) {
        std::cerr << "invalid sdp failed finding end of connection section\n";
        return ;
    }
    line_str = sdp.substr(pos_start, pos_end - pos_start);
    line_vec = split_string(line_str, ' ');
    if (line_vec.size() != 3) {
        std::cerr<<"invalid sdp failed splitting connection line" << line_str << std::endl;
        return ;
    }
    dst_ip = line_vec.back().substr(0, line_vec.back().find("/"));


    try {
        if (!assert_mc_ip(dst_ip, START_AVAILABLE_MC_ADDR_JT_NM, END_AVAILABLE_MC_ADDR_JT_NM)) {
            std::cerr << "Atempting to use a multicast address in the deny list. All multicast addresses should be in the range between: " <<
                START_AVAILABLE_MC_ADDR_JT_NM << " and " << END_AVAILABLE_MC_ADDR_JT_NM << std::endl;
            exit(-1);
        }
    } catch (std::runtime_error &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        exit(-1);
    }

    pos_start = pos_end;
    return check_sdp_dst_ips(sdp.substr(pos_start, sdp.length() - pos_start), pos_start, pos_end);
}

static bool set_clock(rmax_clock_types clock_handler_type, std::vector<std::string> sdp_files)
{
    rmax_clock_t clock;
    memset(&clock, 0, sizeof(clock));

    clock.clock_type = clock_handler_type;

    if (RIVERMAX_SYSTEM_CLOCK == clock_handler_type) {
        p_get_current_time_ns = rivermax_player_time_handler;
        /* Leap sec in nano for TAI conversion to UTC */
        g_tai_to_rmax_time_conversion = (uint64_t)nanoseconds{seconds{LEAP_SECONDS}}.count();
    } else {
        if (RIVERMAX_USER_CLOCK_HANDLER == clock_handler_type) {
            clock.clock_u.rmax_user_clock_handler.clock_handler = rivermax_player_time_handler;
            clock.clock_u.rmax_user_clock_handler.ctx = nullptr;
            p_get_current_time_ns = rivermax_player_time_handler;
        }
        else if (RIVERMAX_PTP_CLOCK == clock_handler_type) {
            std::string src_ip;
            std::ifstream is(sdp_files[0]);
            std::string sdp_file((std::istreambuf_iterator<char>(is)), std::istreambuf_iterator<char>());
            if (!parse_sdp_connection_details(sdp_file, src_ip)) {
                std::cerr << "failed parsing connection info!";
                return false;
            }
            std::cout << "Note: PTP clock time handler is supported with ConnectX-6 Dx or DPU devices only" << std::endl;
            inet_pton(AF_INET, src_ip.c_str(), &clock.clock_u.rmax_ptp_clock.device_ip_addr);
            p_get_current_time_ns = rivermax_time_handler;
        } else {
            std::cerr << "Invalid clock handler type:" << clock_handler_type << std::endl;
            return false;
        }
    }

    rmax_status_t status = rmax_set_clock(&clock);
    if (status != RMAX_OK) {
        std::cout << "failed set clock with status: " << status << std::endl;
        return false;
    }
    return true;
}

int main(int argc, char *argv[])
{
    int ret = 0;

    std::vector<std::string> sdp_files;
    std::vector<std::string> video_files;
    std::vector<int> cpus;
    int rivermax_thread_affinity = CPU_NONE;
    uint16_t max_video_packet_size = 1248;
    uint16_t multiplier = VIDEO_TRO_DEFAULT_MODIFICATION;
    bool allow_v_padding = false;
    std::string streams_to_send;
    int clock_handler_type = rmax_clock_types::RIVERMAX_USER_CLOCK_HANDLER;
    bool assert_mc_addr = false;
    const char *rmax_version = rmax_get_version_string();
    CLI::App app{"Mellanox Rivermax Player" + std::string(rmax_version)};
    app.add_option("-s,--sdp-files", sdp_files, "Comma separated list of SDP files")
        ->delimiter(',')->required()->check(CLI::ExistingFile);
    app.add_option("-m,--media-files", video_files, "Comma separated list of media files")
        ->delimiter(',')->required()->check(CLI::ExistingFile);
    auto loop_opt = app.add_flag("-l,--loop", loop, "Play media files in loop [default: no]");
    app.add_flag("--disable-synchronization", disable_synchronization, "Disable synchronization between video, audio"
         " and ancillary after the first iteration when looping the video file [default: no]")
        ->needs(loop_opt);
    app.add_option("-b,--vid-p-size", max_video_packet_size, "mtu packet size to use for video", true)
        ->check(CLI::Range(1, 1500));
    app.add_option("-p,--stream-type", streams_to_send,
                   "Stream type to play, v:video,a:audio,n:ancillary [default: all]");
    app.add_flag("-a,--allow-padding", allow_v_padding, "add padding to last packet in video frame/fields [default: no]");
    app.add_option("-r,--rivermax-cpu-affinity", rivermax_thread_affinity,
                   "CPU affinity of Rivermax internal thread")->check(CLI::Range(0, 1024));
    app.add_flag("-w,--wait", disable_wait_for_event, "Disable use of rmax_request_notification [default: no]");
    app.add_option("-t,--thread-cpu-affinity", cpus,
                   "Comma separated list of CPU for setting thread affinity. Must contain six cpu IDs\n"
                   "                              - First CPU: to be used for the input video reader thread\n"
                   "                              - Second CPU: to be used for the scaling thread\n"
                   "                              - Third CPU: to be used for the Rivermax sender video thread\n"
                   "                              - Fourth CPU: to be used for the input audio reader thread\n"
                   "                              - Fifth CPU: to be used for the encoding thread\n"
                   "                              - Sixth CPU: to be used for the Rivermax sender audio thread")
    ->delimiter(',')->check(CLI::Range(CPU_NONE, 1024))->type_size(e_num_of_affinity_index);
    app.add_option("-o,--tro-modification", multiplier,
                   "Reduce video default TRO by this number of Trs")->check(CLI::Range(100));
    app.add_option("-v", clock_handler_type, "clock handler type. "
        "1:System clock handler, 2: User Clock handler, 4: PTP Clock handler . [default 2]")
        ->check(CLI::Range((int)RIVERMAX_SYSTEM_CLOCK, (int)RIVERMAX_PTP_CLOCK));
    app.add_flag("--assert-mc_addr", assert_mc_addr, "Check that MC IP address in the range 224.0.2.0 - 239.255.255.255");
    CLI11_PARSE(app, argc, argv);
    if (app.count("-p") > 0) {
        stream_type = 0;
        if (streams_to_send.find('v') != std::string::npos) {
            stream_type |= eMediaType_t::video;
            streams_to_send.erase(std::remove(streams_to_send.begin()
                                              , streams_to_send.end(), 'v'),
                                  streams_to_send.end());
        }
        if (streams_to_send.find('a') != std::string::npos) {
            stream_type |= eMediaType_t::audio;
            streams_to_send.erase(std::remove(streams_to_send.begin()
                                              , streams_to_send.end(), 'a'),
                                  streams_to_send.end());
        }
        if (streams_to_send.find('n') != std::string::npos) {
            stream_type |= eMediaType_t::ancillary;
            streams_to_send.erase(std::remove(streams_to_send.begin()
                                              , streams_to_send.end(), 'n'),
                                  streams_to_send.end());
        }
        if (stream_type == 0 || !streams_to_send.empty()) {
            std::cerr << "invalid media type, options are a,v,n got : " << streams_to_send << std::endl;
            exit(-1);
        }
    }
    if (sdp_files.size() != video_files.size()) {
        std::cout << "Error - Number of SDP files differs from number of media files" << std::endl;
        exit(-1);
    }
    if ((eMediaType_t::ancillary & stream_type) && !(eMediaType_t::video & stream_type)) {
        std::cout << "Error - Ancillary stream should be sent with video stream only" << std::endl;
        exit(-1);
    }

    if (assert_mc_addr) {
        std::ifstream is(sdp_files[0]);
        std::string sdp_file((std::istreambuf_iterator<char>(is)), std::istreambuf_iterator<char>());
        size_t pos_start = 0;
        size_t pos_end = 0;

        try {
            check_sdp_dst_ips(sdp_file, pos_start, pos_end);
        } catch (std::runtime_error &e) {
            std::cerr << "Error: " << e.what() << std::endl;
            exit(-1);
        }
    }

    // Initializes signals caught by the application
    initialize_signals();

    video_tro_default_modification = multiplier;
    rmax_init_config init_config;

    memset(&init_config, 0, sizeof(init_config));

    init_config.flags |= RIVERMAX_HANDLE_SIGNAL;

    if (rivermax_thread_affinity == CPU_NONE) {
        std::cout << "Warning - Rivermax internal thread CPU affinity not set!!!" << std::endl;
    } else {
        RMAX_CPU_SET(rivermax_thread_affinity, &init_config.cpu_mask);
        init_config.flags |= RIVERMAX_CPU_MASK;
    }
    if (cpus.empty()) {
        cpus.resize(e_num_of_affinity_index, CPU_NONE);
    } else {
        if (!rivermax_validate_thread_affinity_cpus(rivermax_thread_affinity, cpus)) {
            std::cout << "Warning - bad cpu affinity" << std::endl;
        }
    }


    rmax_status_t status = rmax_init(&init_config);
    if (status != RMAX_OK) {
        std::cerr << "rmax_init failed with code:" << status << std::endl;
        exit(-1);
    }

    // Set clock
    if(!set_clock((rmax_clock_types)clock_handler_type, sdp_files)) {
        if(!set_clock(rmax_clock_types::RIVERMAX_SYSTEM_CLOCK, sdp_files)) {
            rmax_cleanup();
            exit(-1);
        }
    }

    static std::string media_version = std::to_string(RMAX_MAJOR_VERSION) + std::string(".") +
                                       std::to_string(RMAX_MINOR_VERSION)+ std::string(".") +
                                       std::to_string(RMAX_PATCH_VERSION);

    std::cout<<"#############################################\n";
    std::cout<<"## Rivermax SDK version:        " << rmax_version << std::endl;
    std::cout<<"## Rivermax player version:     " << media_version << std::endl;
    std::cout<<"#############################################\n";

    if (status != RMAX_OK) {
        std::cerr << "Failed starting Rivermax " << status << std::endl;
        rmax_cleanup();
        exit(-1);
    }

#if defined(__linux__) && (LIBAVFORMAT_VERSION_MAJOR <= 59) && (LIBAVFORMAT_VERSION_MINOR < 27)
    // this is actually depends if function is deprecated, in some linux's it already is
    av_register_all();
#endif

    std::vector<std::thread> reader_threads;
    std::vector<std::thread> other_threads;
    std::vector<std::shared_ptr<std::condition_variable>> cond_vars;
    std::vector<std::shared_ptr<AVFormatContext*>> av_format_ctx_vec;
    for (size_t i = 0; i < video_files.size(); ++i) {
        MediaData media_data;
        std::ifstream is(sdp_files[i]);
        std::string sdp((std::istreambuf_iterator<char>(is)), std::istreambuf_iterator<char>());
        std::shared_ptr<std::condition_variable> sync_cv = std::make_shared<std::condition_variable>();
        cond_vars.push_back(sync_cv);
        std::shared_ptr<std::condition_variable> eof_cv = std::make_shared<std::condition_variable>();
        cond_vars.push_back(eof_cv);
        std::shared_ptr<std::atomic<int>> eof_stream_counter = std::make_shared<std::atomic<int>>();
        *eof_stream_counter = 0;
        SynchronizerData sync_data(sync_cv, eof_cv, eof_stream_counter);
        std::shared_ptr<std::mutex> sync_lock = std::make_shared<std::mutex>();

        //Video
        VideoRmaxData video_rmax_data;
        VideoReaderData video_reader_data;
        if (video_process_file(video_files[i].c_str(), video_rmax_data, video_reader_data)) {
            std::cerr << "Fail getting video info" << std::endl;
        }

        if (!parse_video_sdp_params(sdp, media_data)) {
            std::cerr<< "Can't parse video sdp info" << std::endl;
            rmax_cleanup();
            exit(-1);
        }

        if (media_data.height != video_rmax_data.height ||
            media_data.width != video_rmax_data.width ||
            ((uint32_t)(media_data.fps * 1000) != (uint32_t)(video_rmax_data.fps * 1000)) ) {
            std::cerr<< "Provided mp4 file isn't compatible with SDP parameters:" << std::endl;
            rmax_cleanup();
            exit(-1);
        }
        video_rmax_data.fps = media_data.fps;
        video_rmax_data.sample_rate = media_data.sample_rate;

        av_format_ctx_vec.push_back(video_reader_data.p_format_context);
        int video_stream_idx = video_reader_data.stream_index;
        if (video_stream_idx == -1) {
            std::cerr << "Fail finding video stream";
            rmax_cleanup();
            exit(-1);
        }

        double frame_field_start_time_ns = (double)get_tai_time_ns() + (double)nanoseconds{seconds{5}}.count();
        if (eMediaType_t::video & stream_type) {
            //Create threads
            std::shared_ptr<my_queue> video_conv_cb = std::make_shared<my_queue>(CB_SIZE_VIDEO);
            std::shared_ptr<std::condition_variable> video_conv_cv = std::make_shared<std::condition_variable>();
            cond_vars.push_back(video_conv_cv);
            std::shared_ptr<std::mutex> video_conv_lock = std::make_shared<std::mutex>();

            std::shared_ptr<my_queue> video_send_cb = std::make_shared<my_queue>(CB_SIZE_VIDEO);
            std::shared_ptr<std::condition_variable> video_send_cv = std::make_shared<std::condition_variable>();
            cond_vars.push_back(video_send_cv);
            std::shared_ptr<std::mutex> video_send_lock = std::make_shared<std::mutex>();

            video_reader_data.conv_cb = video_conv_cb;
            video_reader_data.conv_cv = video_conv_cv;
            video_reader_data.conv_lock = video_conv_lock;
            video_reader_data.video_type = media_data.video_type;

            sync_data.video_next_frame_field_send_time_ns = std::make_shared<double>(frame_field_start_time_ns);
            sync_data.add_stream();
            video_rmax_data.next_frame_field_send_time_ns = sync_data.video_next_frame_field_send_time_ns;
            video_rmax_data.sdp_path = sdp_files[i];
            video_rmax_data.send_cb = video_send_cb;
            video_rmax_data.send_cv = video_send_cv;
            video_rmax_data.send_lock = video_send_lock;
            video_rmax_data.sync_cv = sync_cv;
            video_rmax_data.sync_lock = sync_lock;
            video_rmax_data.eof_cv = eof_cv;
            video_rmax_data.max_payload_size = max_video_packet_size;
            video_rmax_data.allow_padding = allow_v_padding;
            video_rmax_data.eof_stream_counter = eof_stream_counter;

            bool is_scaler_needed = true;
            if (video_rmax_data.pix_format == AVPixelFormat::AV_PIX_FMT_YUV422P ||
                video_rmax_data.pix_format == AVPixelFormat::AV_PIX_FMT_UYVY422 ||
                video_rmax_data.pix_format == AVPixelFormat::AV_PIX_FMT_YUV422P10LE) {
                is_scaler_needed = false;
                video_reader_data.conv_cb = video_send_cb;
                video_reader_data.conv_cv = video_send_cv;
                video_reader_data.conv_lock = video_send_lock;
                video_send_cb = std::make_shared<my_queue>(2 * CB_SIZE_VIDEO);
            }

            video_reader_data.set_cpu(cpus[e_video_reader_index]);
            reader_threads.emplace_back(read_stream<VideoReaderData>, std::move(video_reader_data));
            if (is_scaler_needed) {
                std::unique_lock<std::mutex> lock(*video_conv_lock);
                video_conv_cv->wait(lock);
            }
            video_rmax_data.video_type = media_data.video_type;
            cst_data time_calculation_data(video_rmax_data.width, video_rmax_data.height,
                                  video_rmax_data.fps, video_rmax_data.video_type, video_rmax_data.pix_format,
                                  video_rmax_data.sample_rate);

            calculate_stream_time(video, video_rmax_data.next_frame_field_send_time_ns, time_calculation_data,
                                  &video_rmax_data.timestamp_tick);
            if (is_scaler_needed) {
                ScaleDataVideo scale_data_video(video_rmax_data, video_conv_cb, video_conv_cv,
                                                video_conv_lock, cpus[e_video_scaler_index]);
                other_threads.emplace_back(scale_video, scale_data_video);

            }
            {
                std::unique_lock<std::mutex> lock(*video_send_lock);
                video_send_cv->wait(lock);
            }

            video_rmax_data.payload_type = media_data.payload_type;
            video_rmax_data.set_cpu(cpus[e_video_sender_index]);
            other_threads.emplace_back(rivermax_video_sender, video_rmax_data);
        }

        AudioRmaxData audio_rmax_data;
        AudioReaderData audio_reader_data;
        if (eMediaType_t::audio & stream_type) {
            //Audio
            if (!parse_audio_sdp_params(sdp, media_data)) {
                std::cout << "No audio stream was found in SDP!" << std::endl;
                ret = -1;
                goto exit;
            }

            if (!audio_process_file(video_files[i].c_str(), audio_rmax_data, audio_reader_data, media_data)) {
                std::cout << "No audio stream was found in file" << std::endl;
                ret = -1;
                goto exit;
            }

            av_format_ctx_vec.push_back(audio_reader_data.p_format_context);
            std::shared_ptr<my_queue> audio_conv_cb = std::make_shared<my_queue>(CB_SIZE_AUDIO);
            std::shared_ptr<std::condition_variable> audio_conv_cv = std::make_shared<std::condition_variable>();
            cond_vars.push_back(audio_conv_cv);
            std::shared_ptr<std::mutex> audio_conv_lock = std::make_shared<std::mutex>();
            std::shared_ptr<my_queue> audio_send_cb = std::make_shared<my_queue>(CB_SIZE_AUDIO);
            std::shared_ptr<std::condition_variable> audio_send_cv = std::make_shared<std::condition_variable>();
            cond_vars.push_back(audio_send_cv);

            std::shared_ptr<std::mutex> audio_send_lock = std::make_shared<std::mutex>();
            audio_reader_data.conv_cb = audio_conv_cb;
            audio_reader_data.conv_cv = audio_conv_cv;
            audio_reader_data.conv_lock = audio_conv_lock;

            sync_data.audio_next_chunk_send_time_ns = std::make_shared<double>(frame_field_start_time_ns);
            sync_data.add_stream();
            audio_rmax_data.next_chunk_send_time_ns = sync_data.audio_next_chunk_send_time_ns;
            audio_rmax_data.sdp_path = sdp_files[i];
            audio_rmax_data.send_cb = audio_send_cb;
            audio_rmax_data.send_cv = audio_send_cv;
            audio_rmax_data.send_lock = audio_send_lock;
            audio_rmax_data.sync_cv = sync_cv;
            audio_rmax_data.sync_lock = sync_lock;
            audio_reader_data.set_cpu(cpus[e_audio_reader_index]);
            audio_rmax_data.ptime_usec = media_data.audio_ptime_us;
            audio_rmax_data.payload_type = media_data.payload_type;
            audio_rmax_data.sample_rate = media_data.sample_rate;
            audio_rmax_data.video_fps = video_rmax_data.fps;
            audio_rmax_data.eof_stream_counter = eof_stream_counter;
            audio_rmax_data.eof_cv = eof_cv;
            if (audio_rmax_data.channels != media_data.channels_num) {
                std::cerr << "Number of channels in SDP differs from number of "
                    "channels in video file. in video file have " <<
                    audio_rmax_data.channels << " in SDP file " <<
                    media_data.channels_num << "\nplease provide matching "
                        "SDP file<->audio file";
                ret = -1;
                goto exit;
            }
            audio_rmax_data.dscp = DSCP_MEDIA_RTP_CLASS;
            audio_rmax_data.bit_depth_in_bytes = media_data.bit_depth / BITS_IN_BYTES;

            cst_data time_calculation_data(audio_rmax_data.ptime_usec,
                                           audio_rmax_data.sample_rate,
                                           video_rmax_data.fps);
            calculate_stream_time(audio, audio_rmax_data.next_chunk_send_time_ns, time_calculation_data,
                             &audio_rmax_data.timestamp_tick);
            AudioEncodeData audio_encode_data(audio_rmax_data, audio_conv_cb, audio_conv_cv,
                                         audio_conv_lock, audio_reader_data.p_codec,
                                         audio_reader_data.p_codec_parameters,
                                         audio_reader_data.stream_index,
                                         cpus[e_audio_encoder_index]);
            reader_threads.emplace_back(read_stream<AudioReaderData>, std::move(audio_reader_data));
            {
                std::unique_lock<std::mutex> lock(*audio_conv_lock);
                audio_conv_cv->wait(lock);
            }
            other_threads.emplace_back(encode_audio, std::move(audio_encode_data));
            {
                std::unique_lock<std::mutex> lock(*audio_send_lock);
                audio_send_cv->wait(lock);
            }
            audio_rmax_data.set_cpu(cpus[e_audio_sender_index]);
            other_threads.emplace_back(rivermax_audio_sender, audio_rmax_data);
        }

        if (eMediaType_t::ancillary & stream_type) {
            if (!parse_anc_sdp_params(sdp, media_data)) {
                std::cout << "No ancillary stream was found in SDP!" << std::endl;
                ret = -1;
                goto exit;
            }

            //Ancillary
            AncillaryRmaxData ancillary_rmax_data;
            sync_data.ancillary_next_chunk_send_time_ns = std::make_shared<double>(frame_field_start_time_ns);
            sync_data.add_stream();
            ancillary_rmax_data.next_chunk_send_time_ns = sync_data.ancillary_next_chunk_send_time_ns;
            ancillary_rmax_data.sdp_path = sdp_files[i];
            ancillary_rmax_data.fps = video_rmax_data.fps;
            ancillary_rmax_data.video_type = media_data.video_type;
            ancillary_rmax_data.video_sample_rate = video_rmax_data.sample_rate;
            ancillary_rmax_data.video_width = video_rmax_data.width;
            ancillary_rmax_data.video_height =video_rmax_data.height;
            ancillary_rmax_data.video_duration_sec = video_rmax_data.duration;
            ancillary_rmax_data.video_pix_format = video_rmax_data.pix_format;
            ancillary_rmax_data.eof_stream_counter = eof_stream_counter;
            ancillary_rmax_data.sync_cv = sync_cv;
            ancillary_rmax_data.sync_lock = sync_lock;
            ancillary_rmax_data.eof_cv = eof_cv;
            cst_data time_calculation_data(video_rmax_data.width, video_rmax_data.height,
                video_rmax_data.fps, media_data.video_type, video_rmax_data.pix_format, video_rmax_data.sample_rate);
            calculate_stream_time(eMediaType_t::ancillary, ancillary_rmax_data.next_chunk_send_time_ns,
                                  time_calculation_data, nullptr);
            ancillary_rmax_data.payload_type = media_data.payload_type;
            ancillary_rmax_data.did = media_data.did;
            ancillary_rmax_data.sdid = media_data.sdid;
            other_threads.emplace_back(rivermax_ancillary_sender, ancillary_rmax_data);
        }
        if (loop && !disable_synchronization) {
            other_threads.emplace_back(SynchronizerData::streams_synchronizer, sync_data);
        }
    }

exit:
    if (ret) {
        std::cout << "Terminating threads..." << std::endl;
        run_threads = false;
    }

    for (auto &t : reader_threads) {
        t.join();
    }
    for (auto &t : other_threads) {
        t.join();
    }

    for (auto t : av_format_ctx_vec) {
        if (t != nullptr) {
            avformat_close_input(t.get());
        }
    }

    reader_threads.clear();
    other_threads.clear();
    cond_vars.clear();
    av_format_ctx_vec.clear();

    rmax_cleanup();
    return ret;
}
