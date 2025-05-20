/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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
#include "rdk/rivermax_dev_kit.h"

using namespace rivermax::dev_kit::services;


RmxOutMediaAPIBaseDemoApp::RmxOutMediaAPIBaseDemoApp(const std::string& app_description, const std::string& app_examples) :
    RmxAPIBaseDemoApp(app_description, app_examples)
{
}

void RmxOutMediaAPIBaseDemoApp::add_cli_options()
{
    RmxAPIBaseDemoApp::add_cli_options();

    m_cli_parser_manager->add_option(CLIOptStr::DST_IP);
    m_cli_parser_manager->add_option(CLIOptStr::DST_PORT);
}

void RmxOutMediaAPIBaseDemoApp::post_cli_parse_initialization()
{
    auto& s = m_app_settings;

    s->num_of_total_streams = 1;

    s->media.resolution = Resolution{FHD_WIDTH, FHD_HEIGHT};
    s->media.frame_rate = {60, 1};
    s->media.sampling_type = VideoSampling::YCbCr_4_2_2;
    s->media.bit_depth = ColorBitDepth::_10;
    s->media.video_scan_type = VideoScanType::Progressive;
    s->media.sender_type = SenderType::_2110TPN;
    s->media.sample_rate = 90000;
    s->media.frames_fields_in_mem_block = 1;

    auto status = initialize_media_settings(*s);
    if (status != ReturnStatus::success) {
        throw std::runtime_error("Failed to initialize media settings");
    }
}
