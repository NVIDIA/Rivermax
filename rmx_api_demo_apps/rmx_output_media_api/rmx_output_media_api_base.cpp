/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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
#include <cinttypes>
#include <string>

#include "rmx_output_media_api_base.h"
#include "api/rmax_apps_lib_api.h"

using namespace ral::lib::services;


RmxOutMediaAPIBaseDemoApp::RmxOutMediaAPIBaseDemoApp(const std::string& app_description, const std::string& app_examples) :
    RmxAPIBaseDemoApp(app_description, app_examples)
{
}

void RmxOutMediaAPIBaseDemoApp::add_cli_options()
{
    RmxAPIBaseDemoApp::add_cli_options();

    m_cli_parser_manager->add_option(CLIOptStr::DST_IP);
    m_cli_parser_manager->add_option(CLIOptStr::DST_PORT);
    m_cli_parser_manager->get_parser()->add_option(
        "-x,--stream-type",
        m_app_settings->video_stream_type,
        "Type of stream")->check(CLI::IsMember(SUPPORTED_STREAMS))
        ->required()->default_val(VIDEO_2110_20_1080p60);
}

void RmxOutMediaAPIBaseDemoApp::post_cli_parse_initialization()
{
    auto& s = m_app_settings;
    m_app_settings->num_of_total_flows = 1;

    if (s->video_stream_type.compare(VIDEO_2110_20_1080p60) == 0) {
        s->media.resolution = { FHD_WIDTH, FHD_HEIGHT };
    } else if (s->video_stream_type.compare(VIDEO_2110_20_2160p60) == 0) {
        s->media.resolution = { UHD_WIDTH, UHD_HEIGHT };
    } else {
        throw std::runtime_error("Unsupported stream type");
    }
    s->media.media_block_index = 0;
    s->media.video_scan_type = VideoScanType::Progressive;
    s->media.sample_rate = 90000;
    s->media.frame_rate = { 60, 1 };
    s->media.packets_in_frame_field = HD_PACKETS_PER_FRAME_422_10B * \
        (s->media.resolution.width / FHD_WIDTH) * \
        (s->media.resolution.height / FHD_HEIGHT);

    s->num_of_memory_blocks = 1;
    s->packet_payload_size = RTP_YUV_DEFAULT_PAYLOAD_SIZE + RTP_HEADER_SIZE;

    constexpr size_t lines_in_chunk = 4;
    s->media.packets_in_line = s->media.packets_in_frame_field / s->media.resolution.height;
    s->num_of_packets_in_chunk = lines_in_chunk * s->media.packets_in_line;

    s->media.frame_field_time_interval_ns = NS_IN_SEC / static_cast<double>(
        s->media.frame_rate.num / s->media.frame_rate.denom);
    s->media.lines_in_frame_field = s->media.resolution.height;
    s->media.chunks_in_frame_field = static_cast<size_t>(std::ceil(
        s->media.packets_in_frame_field / static_cast<double>(s->num_of_packets_in_chunk)));
    s->media.frames_fields_in_mem_block = 1;
    s->num_of_chunks_in_mem_block = s->media.frames_fields_in_mem_block * s->media.chunks_in_frame_field;
    s->num_of_packets_in_mem_block = s->num_of_chunks_in_mem_block * s->num_of_packets_in_chunk;

    std::stringstream sdp;
    sdp << "v=0\n"
        << "o=- 1443716955 1443716955 IN IP4 " << s->local_ip << "\n"
        << "s=SMPTE ST2110-20 narrow gap " << s->video_stream_type << "\n"
        << "t=0 0\n"
        << "m=video " << s->destination_port << " RTP/AVP 96\n"
        << "c=IN IP4 " << s->destination_ip << "/64\n"
        << "a=source-filter: incl IN IP4 " << s->destination_ip << " " << s->local_ip << "\n"
        << "a=rtpmap:96 raw/" << s->media.sample_rate << "\n"
        << "a=fmtp:96 sampling=YCbCr-4:2:2; width="
        << s->media.resolution.width << "; height=" << s->media.resolution.height
        << "; exactframerate=" << static_cast<double>(s->media.frame_rate.num / s->media.frame_rate.denom)
        << "; depth=10; TCS=SDR; colorimetry=BT709; PM=2110GPM; SSN=ST2110-20:2017; TP=2110TPN;\n"
        << "a=mediaclk:direct=0\n"
        << "a=ts-refclk:localmac=40-a3-6b-a0-2b-d2";

    s->media.sdp = sdp.str();
}
