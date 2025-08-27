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
    RmxAPIBaseDemoApp(app_description, app_examples),
    m_video_settings(std::make_shared<SMPTE_2110_20_MediaSettings>())
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

    m_video_settings->resolution = Resolution{FHD_WIDTH, FHD_HEIGHT};
    m_video_settings->frame_rate = {60, 1};
    m_video_settings->sampling_type = VideoSampling::YCbCr_4_2_2;
    m_video_settings->bit_depth = VideoBitDepth::_10;
    m_video_settings->video_scan_type = VideoScanType::Progressive;
    m_video_settings->sample_rate = 90000;
    m_video_settings->frames_fields_in_mem_block = 1;

    m_video_settings->media_settings_calculator = IMediaSettingsCalculatorFactory::get_media_settings_calculator(*m_video_settings);
    if (!m_video_settings->media_settings_calculator) {
        std::cerr << "Failed to get media settings calculator" << std::endl;
        return;
    }

    ReturnStatus calc_status = m_video_settings->media_settings_calculator->calculate_media_settings();
    if (calc_status != ReturnStatus::success) {
        std::cerr << "Failed to calculate media settings" << std::endl;
        return;
    }

    s->media.sdp = m_video_settings->media_settings_calculator->generate_media_sdp(
        s->local_ip,
        s->source_port,
        s->destination_ip,
        s->destination_port
    );
}
