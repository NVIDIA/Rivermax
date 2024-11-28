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
#ifndef RMX_API_DEMO_APPS_RMX_OUTPUT_MEDIA_API_RMX_MEDIA_SEND_MEM_ALLOC_RGSTR_RMX_MEDIA_SEND_MEM_ALLOC_RGSTR_H_
#define RMX_API_DEMO_APPS_RMX_OUTPUT_MEDIA_API_RMX_MEDIA_SEND_MEM_ALLOC_RGSTR_RMX_MEDIA_SEND_MEM_ALLOC_RGSTR_H_

#include "rmx_output_media_api_base.h"
#include "api/rmax_apps_lib_api.h"

using namespace ral::lib::services;

#define HUGE_PAGE_SIZE_LOG2 21 // 2 MB

constexpr const char* APP_DESCRIPTION = "NVIDIA Rivermax API Media Send Demo App For Memory Allocation And Registration ";
constexpr const char* APP_EXAMPLES = \
    "Examples:\n"
    "  1. rmx_media_send_mem_alloc_rgstr --local-ip 1.2.3.4 --stream-type 1080p60\n"
    "  2. rmx_media_send_mem_alloc_rgstr --local-ip 1.2.3.4 --stream-type 1080p60 --dst-ip 231.1.2.3 --dst-port 60000";

/**
 * @brief Basic Rivermax API Media Send Demo Application For Memory Allocation And Registration.
 *
 * This application demonstrates the most basic usage of the Rivermax Media Send API functionality.
 * using Memory Allocation And Registration.
 */
class RmxMediaSendMemAllocRgstr : public RmxOutMediaAPIBaseDemoApp
{
public:
    RmxMediaSendMemAllocRgstr() : RmxOutMediaAPIBaseDemoApp(APP_DESCRIPTION, APP_EXAMPLES) {}
    virtual ~RmxMediaSendMemAllocRgstr() = default;
protected:
    virtual ReturnStatus operator()() override;
};

#endif // RMX_API_DEMO_APPS_RMX_OUTPUT_MEDIA_API_RMX_MEDIA_SEND_MEM_ALLOC_RGSTR_RMX_MEDIA_SEND_MEM_ALLOC_RGSTR_H_
