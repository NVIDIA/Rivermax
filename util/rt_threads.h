/*
 * SPDX-FileCopyrightText: Copyright (c) 2022-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#ifndef _RT_THREADS_H_
#define _RT_THREADS_H_
#include <rivermax_api.h>
#include <vector>
#include <iostream>
#include <string>
#include <sstream>
#include <cstring>
#include <cmath>
#include <chrono>
#include <csignal>
#include <atomic>
#include <map>
#include <utility>
#include <functional>
#include "rational.h"
#define CPU_NONE (-1)
#define MAX_CPU_RANGE 1024

#ifdef __linux__
#define RMAX_THREAD_PRIORITY_TIME_CRITICAL 0
#define INVALID_HANDLE_VALUE (-1)
#else
#define RMAX_THREAD_PRIORITY_TIME_CRITICAL THREAD_PRIORITY_TIME_CRITICAL
#endif

#if !defined(_WIN32) && !defined(_WIN64)
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/mman.h>
#endif

#define RMAX_CPUELT(_cpu)  ((_cpu) / RMAX_NCPUBITS)
#define RMAX_CPUMASK(_cpu) ((rmax_cpu_mask_t) 1 << ((_cpu) % RMAX_NCPUBITS))
#define RMAX_CPU_SET(_cpu, _cpusetp) \
    do { \
        size_t _cpu2 = (_cpu); \
        if (_cpu2 < (8 * sizeof (rmax_cpu_set_t))) { \
            (((rmax_cpu_mask_t *)((_cpusetp)->rmax_bits))[RMAX_CPUELT(_cpu2)] |= \
                                      RMAX_CPUMASK(_cpu2)); \
        } \
    } while (0)

enum FONT_COLOR {
    COLOR_RED,
    COLOR_RESET,
};

const std::map<std::string, std::pair<std::string, Rational>> supported_fps_map = {
    {
        std::string("23.976"),
        {std::string("24000/1001"), Rational(24000, 1001)}
    },
    {
        std::string("24"),
        {std::string("24"), Rational(24)}
    },
    {
        std::string("25"), 
        {std::string("25"), Rational(25)}
    },
    {
        std::string("29.97"),
        {std::string("30000/1001"), Rational(30000, 1001)}
    },
    {
        std::string("30"),
        {std::string("30"), Rational(30)}
    },
    {
        std::string("50"),
        {std::string("50"), Rational(50)}
    },
    {
        std::string("59.94"),
        {std::string("60000/1001"), Rational(60000, 1001)}
    },
    {
        std::string("60"),
        {std::string("60"), Rational(60)}
    },
};

void register_signal_handler_cb(const std::function<void()>& callback);
/**
 * @brief: Returns the exit status of the application.
 *
 * This method is responsible to return the std::atomic_bool
 * exit status of the application.
 * It should be used by any component of the application that is doing
 * long period runs.
 */
std::atomic_bool& exit_app();
extern std::atomic_bool g_s_signal_received;
/**
* @brief: Initializes signals caught by the application.
*/
void initialize_signals();
/**
* @brief: Signal handler of the application.
*
* It will notify the user about the signal caught and will set @ref g_s_signal_received
* std::atomic_bool parameter to true.
*
* @param [in] signal_num: Number of the received signal.
*/
void signal_handler(const int signal_num);

void *color_set(enum FONT_COLOR color);
void color_reset(void *ctx);
bool cpu_affinity_get(std::stringstream &s, long &ret);
bool rivermax_validate_thread_affinity_cpus(int internal_thread_affinity, std::vector<int> &cpus);
bool rt_set_thread_affinity(struct rmax_cpu_set_t *cpu_mask);
void rt_set_thread_affinity(const std::vector<int>& cpu_core_affinities);
bool rt_set_rivermax_thread_affinity(int cpu_core);
int rt_set_realtime_class(void);
int rt_set_thread_priority(int prio);
uint16_t get_cache_line_size(void);
uint16_t get_page_size(void);

class EventMgr
{
public:
    EventMgr();
    ~EventMgr();
    EventMgr(EventMgr&) =delete;
    EventMgr& operator=(const EventMgr&) =delete;

    bool init(rmx_stream_id stream_id);
    bool request_notification(rmx_stream_id stream_id);
    int wait_for_notification(rmx_stream_id stream_id);

private:
    int init_event_manager(rmx_event_channel_handle event_channel_handle);
    void on_completion() { m_request_completed = true; }
    int bind_event_channel(rmx_event_channel_handle event_channel_handle);
    rmx_stream_id m_stream_id;
#ifdef __linux__
    int m_epoll_fd;
#else
    OVERLAPPED m_overlapped;
    static HANDLE m_iocp;
#endif
    rmx_event_channel_handle m_event_channel_handle;
    bool m_request_completed;
};

inline std::vector<std::string> split_string(const std::string &s, char delim) {
    std::vector<std::string> elems;
    // Check to see if empty string, give consistent result
    if(s.empty())
        elems.emplace_back();
    else {
        std::stringstream ss;
        ss.str(s);
        std::string item;
        while(std::getline(ss, item, delim)) {
            elems.push_back(item);
        }
    }
    return elems;
}

enum STREAM_TYPE {
    VIDEO_2110_20_STREAM,
    VIDEO_2110_22_STREAM,
    AUDIO_2110_30_31_STREAM,
    ANCILLARY_2110_40_STREAM,
    SMPTE_2022_6_STREAM,
    SMPTE_2022_8_STREAM,
};

enum TP_MODE {
    TPN,
    TPNL,
    TPW,
};

enum VIDEO_TYPE {
    PROGRESSIVE = 1,
    INTERLACE = 2,
    NON_VIDEO = 1,
};

enum SAMPLING_TYPE {
    YCBCR422,
    YCBCR444,
    RGB,
    NONE
};

//Defining bytes per pixel ratios
#define RGB_8_BPP_PIXELS 1
#define RGB_8_BPP_BYTES 3
#define RGB_10_BPP_PIXELS 4
#define RGB_10_BPP_BYTES 15
#define RGB_12_BPP_PIXELS 2
#define RGB_12_BPP_BYTES 9
#define YUV422_8_BPP_PIXELS 2
#define YUV422_8_BPP_BYTES 4
#define YUV422_10_BPP_PIXELS 2
#define YUV422_10_BPP_BYTES 5
#define YUV422_12_BPP_PIXELS 2
#define YUV422_12_BPP_BYTES 6

#define RGB_8BIT_PAYLOAD_SIZE 1152  //5 packages per line at FHD
#define RGB_10BIT_PAYLOAD_SIZE 1200 //6 packages per line at FHD
#define RGB_12BIT_PAYLOAD_SIZE 1080 //8 packages per line at FHD
#define YUV_DEFAULT_PAYLOAD_SIZE 1200

#define DEFAULT_BPP_PIXELS 2
#define DEFAULT_BPP_BYTES 5

template<typename T>
static bool parse_payload_type_and_port_params(const std::string &media_line, T &stream_data)
{
    std::vector<std::string> line_vec;
    line_vec = split_string(media_line, ' ');
    if (line_vec.size() != 4) {
        std::cerr<<"invalid sdp failed finding audio payload type\n";
        return false;
    }

    stream_data.payload_type = (uint8_t)stoi(line_vec[3]);
    stream_data.dst_port = (uint16_t)stoi(line_vec[1]);
    return true;
}

template<typename T>
static bool parse_audio_sdp_params(const std::string &sdp, T &stream_data)
{
    size_t media_start;
    size_t pos_start;
    size_t pos_end;
    std::string line_str;
    std::vector<std::string> line_vec;

    if ((pos_start = sdp.find("m=audio")) == std::string::npos) {
        std::cerr<<"invalid sdp failed finding audio media section\n";
        return false;
    }

    media_start = pos_start;

    if ((pos_end = sdp.find_first_of("\r\n", pos_start)) == std::string::npos) {
        std::cerr<<"invalid sdp failed finding end of audio media section\n";
        return false;
    }
    line_str = sdp.substr(pos_start, pos_end - pos_start);
    if (!parse_payload_type_and_port_params(line_str, stream_data)) {
        return false;
    }

    if ((pos_start = sdp.find("a=rtpmap", media_start)) == std::string::npos) {
        std::cerr<<"invalid sdp failed finding audio attribute\n";
        return false;
    }
    if ((pos_end = sdp.find_first_of("\r\n", pos_start)) == std::string::npos) {
        std::cerr<<"invalid sdp failed finding end of audio attribute\n";
        return false;
    }
    line_str = sdp.substr(pos_start, pos_end - pos_start);
    line_vec = split_string(line_str, ' ');
    if (line_vec.size() < 1) {
        std::cerr<<"invalid sdp failed finding audio attribute\n";
       return false;
    }
    line_vec = split_string(line_vec[1], '/');
    if (line_vec.size() != 3) {
        std::cerr<<"invalid audio parameters "<< line_str;
        return false;
    }
    if (line_vec[0].find("AM824") == std::string::npos) {
        stream_data.bit_depth = (uint16_t)stoi(line_vec[0].substr(1));
    } else {
        stream_data.bit_depth = 32;
    }
    stream_data.sample_rate = stoi(line_vec[1]);
    stream_data.channels_num = stoi(line_vec[2]);
    if ((pos_start = sdp.find("ptime:", media_start)) == std::string::npos) {
        std::cerr<<"invalid sdp failed finding ptime attribute\n";
        return false;
    }
    stream_data.audio_ptime_us = (uint32_t)(stod(sdp.substr(pos_start + strlen("ptime:"))) * 1000);
    if (!stream_data.audio_ptime_us || !stream_data.bit_depth || !stream_data.channels_num ||
        !stream_data.sample_rate) {
        return false;
    }

    return true;
}

template<typename T>
bool parse_video_frame_rate_numeric(const std::string &frame_rate_string, T &frame_rate)
{
    if (frame_rate_string.find(".") == std::string::npos) {
        frame_rate = stoi(frame_rate_string);
    } else {
        auto idx = frame_rate_string.find_last_of("0123456789");
        if (idx != std::string::npos)
            ++idx;
        std::string frame_rate_string_trim(frame_rate_string, 0, idx);
        auto fps = supported_fps_map.find(frame_rate_string_trim);
        if (fps == supported_fps_map.end()) {
            std::cerr << "invalid framerate " << frame_rate_string << std::endl;
            return false;
        }
        frame_rate = rational_cast<T>(fps->second.second);
    }
    return true;
}

template<typename T>
bool parse_video_frame_rate(const std::string &frame_rate_string, T &frame_rate, bool is_2022_6)
{
    if (is_2022_6) {
        return parse_video_frame_rate_numeric(frame_rate_string, frame_rate);
    } else {
        if (frame_rate_string.find("/") != std::string::npos) {
            std::vector<std::string> nume_deno = split_string(frame_rate_string, '/');
            if (nume_deno.size() != 2) {
                std::cerr << "invalid framerate " << frame_rate_string << std::endl;
                return false;
            }
            frame_rate = stoi(nume_deno[0]);
            frame_rate /= stoi(nume_deno[1]);
        } else {
            frame_rate = stoi(frame_rate_string);
        }
    }
    return true;
}

template<typename T>
bool parse_video_rtpmap_param(const std::string &sdp, T &stream_data, size_t &media_start)
{
    size_t pos_start, pos_end;
    std::string line_str;
    std::vector<std::string> line_vec;

    if ((pos_start = sdp.find("a=rtpmap", media_start)) == std::string::npos) {
        std::cerr << "invalid sdp failed finding video rtpmap attribute" << std::endl;
        return false;
    }
    if ((pos_end = sdp.find_first_of("\r\n", pos_start)) == std::string::npos) {
        std::cerr << "invalid sdp failed finding end of video rtpmap attribute end" << std::endl;
        return false;
    }
    line_str = sdp.substr(pos_start, pos_end - pos_start);
    line_vec = split_string(line_str, ' ');
    if (line_vec.size() < 1) {
        std::cerr << "invalid sdp failed parsing video rtpmap attribute" << std::endl;
        return false;
    }
    line_vec = split_string(line_vec[1], '/');
    if (line_vec.size() != 2) {
        std::cerr << "invalid video parameters " << line_str << std::endl;
        return false;
    }
    stream_data.sample_rate = stoi(line_vec[1]);
    return true;
}

template<typename T>
bool parse_video_fmtp_param(const std::string &sdp, T &stream_data, size_t &pos_start, size_t &pos_end)
{
    std::string line_str, frame_rate;
    std::vector<std::string> line_vec;
    stream_data.video_type = VIDEO_TYPE::PROGRESSIVE;

    line_str = sdp.substr(pos_start, pos_end - pos_start);
    line_vec = split_string(line_str, ' ');
    for (const auto& token : line_vec) {
        if (token.find("depth=") != std::string::npos)
        {
            stream_data.depth = (uint16_t)stoi(token.substr(strlen("depth=")));
            continue;
        }
        if (token.find("sampling=") != std::string::npos)
        {
            if (token.find("YCbCr-4:2:2") != std::string::npos) {
                stream_data.sampling = static_cast<decltype(stream_data.sampling)>(YCBCR422);
            }
            else if (token.find("RGB") != std::string::npos) {
                stream_data.sampling = static_cast<decltype(stream_data.sampling)>(RGB);
            }
            else {
                return false;
            }
            continue;
        }
        if (token.find("width=") != std::string::npos) {
            stream_data.width = (uint16_t)stoi(token.substr(strlen("width=")));
            continue;
        }
        if (token.find("height=") != std::string::npos) {
            stream_data.height = (uint16_t)stoi(token.substr(strlen("height=")));
            continue;
        }
        if (token.find("TP=") != std::string::npos) {
            if (token.find("TPNL") != std::string::npos) {
                stream_data.tp_mode = static_cast<decltype(stream_data.tp_mode)>(TPNL);
            } else if (token.find("TPW") != std::string::npos) {
                stream_data.tp_mode = static_cast<decltype(stream_data.tp_mode)>(TPW);
            } else if (token.find("TPN") != std::string::npos) {
                stream_data.tp_mode = static_cast<decltype(stream_data.tp_mode)>(TPN);
            } else {
                std::cerr << "Invalid TP= parameter value for a=fmtp attribute." << std::endl;
                return false;
            }
            continue;
        }
        if (token.find("exactframerate=") != std::string::npos) {
            frame_rate = split_string(token, '=')[1];
            continue;
        }
        if (token.find("CMAX=") != std::string::npos) {
            stream_data.cmax = (uint16_t)stoi(token.substr(strlen("CMAX=")));
            continue;
        }
        if (token.find("interlace") != std::string::npos) {
            stream_data.video_type = VIDEO_TYPE::INTERLACE;
        }
    }
    if (!frame_rate.empty()) {
        if (!parse_video_frame_rate(frame_rate, stream_data.fps, false)) {
            return false;
        }
    }
    return true;
}


/* Media description, if present (RFC4566)
     m=  (media name and transport address)
     i=* (media title)
     c=* (connection information -- optional if included at session level)
     b=* (zero or more bandwidth information lines)
     k=* (encryption key)
     a=* (zero or more media attribute lines)
*/
template<typename T>
bool parse_video_sdp_params(const std::string &sdp, T &stream_data)
{
    size_t media_start;
    size_t pos_start;
    size_t pos_end;
    std::string line_str;
    std::vector<std::string> line_vec;
    std::string frame_rate;
    bool has_fmtp, is_smpte2022_6;

    if ((media_start = sdp.find("m=video")) == std::string::npos) {
        std::cerr << "invalid sdp failed finding video starting media section" << std::endl;
        return false;
    }

    if ((pos_end = sdp.find_first_of("\r\n", media_start)) == std::string::npos) {
        std::cerr << "invalid sdp failed finding end of video media section" << std::endl;
        return false;
    }

    line_str = sdp.substr(media_start, pos_end - media_start);
    if (!parse_payload_type_and_port_params(line_str, stream_data)) {
        return false;
    }

    if ((pos_start = sdp.find("b=AS:")) != std::string::npos) {
        if ((pos_end = sdp.find_first_of("\r\n", pos_start)) == std::string::npos) {
            std::cerr << "invalid sdp failed finding end of video bandwidth section" << std::endl;
            return false;
        }
        stream_data.bw_as = (uint32_t)stoul(split_string(sdp.substr(pos_start, pos_end - pos_start), ':')[1]);
    }

    if (!parse_video_rtpmap_param(sdp, stream_data, media_start)) {
        return false;
    }

    has_fmtp = (pos_start = sdp.find("a=fmtp", media_start)) != std::string::npos;
    is_smpte2022_6 = sdp.find("SMPTE2022-6") != std::string::npos;

    if (!is_smpte2022_6 && !has_fmtp) { //-21 and -22 are obliged to have a=fmtp
        std::cerr << "invalid sdp failed finding video fmtp attribute" << std::endl;
        return false;
    }

    if (has_fmtp) { // either -8, -21 or -22
        if ((pos_end = sdp.find_first_of("\r\n", pos_start)) == std::string::npos) {
            std::cerr << "invalid sdp failed finding end of fmtp attribute" << std::endl;
            return false;
        }
        if (!parse_video_fmtp_param(sdp, stream_data, pos_start, pos_end)) {
            std::cerr << "failed parsing fmtp attribute" << std::endl;
            return false;
        }
    }

    if (!stream_data.fps) { // either -6 or -8, or possibly -22 (has either exactframerate= or a=framerate)
        if ((pos_start = sdp.find("a=framerate:")) != std::string::npos) {
            if ((pos_end = sdp.find_first_of("\r\n", pos_start)) == std::string::npos) {
                std::cerr << "invalid sdp failed finding end of video media section" << std::endl;
                return false;
            }
            frame_rate = split_string(sdp.substr(pos_start, pos_end - pos_start), ':')[1];
            parse_video_frame_rate(frame_rate, stream_data.fps, true);
        }
    }

    if (!(stream_data.fps)) { // must be set for -6, -8, -21, -22 (either by exactframerate= or a=framerate
        std::cerr << "failed parsing parameters found fps: " << stream_data.fps << std::endl;
        return false;
    }

    if (is_smpte2022_6) {
        return true;
    }

    if (!stream_data.width || !stream_data.height) { // either -21 or -22
        std::cerr << "failed parsing parameters found width:" << stream_data.width << " height:"
            << stream_data.height << std::endl;
        return false;
    }

    if (sdp.find("raw/90000") == std::string::npos) { // -22
        if (!stream_data.bw_as) {
            std::cerr << "invalid sdp failed finding video bandwidth section" << std::endl;
            return false;
        }
    }
    return true;
}

template<typename T>
static bool parse_anc_sdp_params(const std::string &sdp, T &stream_data)
{
    size_t media_start;
    size_t pos_start;
    size_t pos_end;
    std::string line_str;
    std::vector<std::string> line_vec;

    if ((pos_start = sdp.find("m=")) == std::string::npos) {
        std::cerr<<"invalid sdp failed finding media sections\n";
        return false;
    }

    if ((pos_start = sdp.find("smpte291/90000"), pos_start) == std::string::npos) {
        std::cerr<<"invalid sdp failed finding ancillary media section\n";
        return false;
    }

    if ((pos_start = sdp.rfind("m=video", pos_start)) == std::string::npos) {
        std::cerr<<"invalid sdp failed finding ancillary starting media section\n";
        return false;
    }

    media_start = pos_start;

    if ((pos_end = sdp.find_first_of("\r\n", media_start)) == std::string::npos) {
        std::cerr<<"invalid sdp failed finding end of ancillary media section\n";
        return false;
    }

    line_str = sdp.substr(media_start, pos_end - media_start);
    if (!parse_payload_type_and_port_params(line_str, stream_data)) {
        return false;
    }
    if ((pos_start = sdp.find("a=fmtp", media_start)) == std::string::npos) {
        // all parameters are optional in RFC 8331 / ST 2110-40 so fmtp is not required
        return true;
    }
    if ((pos_start = sdp.find("DID_SDID=", pos_start)) == std::string::npos) {
        std::cerr<<"invalid sdp failed finding ancillary DID_SDID parameter\n";
        return false;
    }
    if ((pos_start = sdp.find("{", pos_start)) == std::string::npos) {
        std::cerr<<"invalid sdp failed finding DID_SDID opening brace\n";
        return false;
    }

    if ((pos_end = sdp.find("}", pos_start)) == std::string::npos) {
            std::cerr<<"invalid sdp failed finding end of fmtp attribute\n";
        std::cerr<<"invalid sdp failed finding DID_SDID closing brace\n";
        return false;
    }

    line_str = sdp.substr(pos_start + 1, pos_end - pos_start - 1);
    line_vec = split_string(line_str, ',');
    if (line_vec.size() != 2) {
        std::cerr<<"invalid sdp failed finding DID and SDID parameters\n";
       return false;
    }

    stream_data.did = (uint16_t)(stod(line_vec[0]));
    stream_data.sdid = (uint16_t)(stod(line_vec[1]));

    return true;
}

constexpr uint8_t DEFAULT_LEAP_SECONDS = 37;

uint64_t default_time_handler(void*); // used time handler forward declaration
static uint64_t(*p_get_time_ns)(void *ctx) = default_time_handler;

inline uint64_t get_time_ns(void)
{
    return p_get_time_ns(nullptr);
}

double time_to_rtp_timestamp(double time_ns, int sample_rate);

uint32_t convert_ip_str_to_int(const std::string& ipv4str);

bool assert_mc_ip(std::string ipv4str, std::string start_ipv4str, std::string end_ipv4str);

std::string get_local_time(uint64_t time_ns);

int set_enviroment_variable(const std::string &name, const std::string &value);

rmx_status rivermax_setparam(const std::string &name, const std::string &value, bool final);

bool rivermax_setparams(const std::vector<std::string> &assignments);

#endif // _RT_THREADS_H_

