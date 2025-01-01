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
#ifndef RMX_API_DEMO_APPS_RMX_OUTPUT_MEDIA_API_RMX_OUTPUT_MEDIA_API_BASE_H_
#define RMX_API_DEMO_APPS_RMX_OUTPUT_MEDIA_API_RMX_OUTPUT_MEDIA_API_BASE_H_

#include <cinttypes>
#include <chrono>
#include <string>
#include <unordered_set>

#include "rmx_api_base_demo_app.h"

constexpr size_t RTP_YUV_DEFAULT_PAYLOAD_SIZE = 1200;
constexpr size_t RTP_HEADER_SIZE = 12 + 8;
constexpr size_t HD_PACKETS_PER_FRAME_422_10B = 4320;
constexpr const char* VIDEO_2110_20_1080p60 = "1080p60";
constexpr const char* VIDEO_2110_20_2160p60 = "2160p60";
constexpr uint16_t FHD_WIDTH = 1920;
constexpr uint16_t FHD_HEIGHT = 1080;
constexpr uint16_t UHD_HEIGHT = 2160;
constexpr uint16_t UHD_WIDTH = 3840;
constexpr size_t NS_IN_SEC = std::chrono::nanoseconds{ std::chrono::seconds{ 1 } }.count();
const std::unordered_set<const char*> SUPPORTED_STREAMS = { VIDEO_2110_20_1080p60, VIDEO_2110_20_2160p60 };

/**
 * @brief Base class for all output media API demo applications.
 *
 * This class provides the base functionality for all output media API demo applications.
 * It provides common command line options and application initializations.
 */
class RmxOutMediaAPIBaseDemoApp : public RmxAPIBaseDemoApp
{
protected:
    RmxOutMediaAPIBaseDemoApp(const std::string& app_description, const std::string& app_examples);
    virtual ~RmxOutMediaAPIBaseDemoApp() = default;

    virtual void add_cli_options() override;
    virtual void post_cli_parse_initialization() override;
};

#endif // RMX_API_DEMO_APPS_RMX_OUTPUT_MEDIA_API_RMX_OUTPUT_MEDIA_API_BASE_H_
