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
#ifndef RMX_API_DEMO_APPS_RMX_OUTPUT_GENERIC_API_RMX_GENERIC_SEND_DEMO_APP_RMX_GENERIC_SEND_DEMO_APP_H_
#define RMX_API_DEMO_APPS_RMX_OUTPUT_GENERIC_API_RMX_GENERIC_SEND_DEMO_APP_RMX_GENERIC_SEND_DEMO_APP_H_

#include "rmx_output_generic_api_base.h"
#include "api/rmax_apps_lib_api.h"

using namespace ral::lib::services;


constexpr const char* APP_DESCRIPTION = "NVIDIA Rivermax API Generic Send Demo App ";
constexpr const char* APP_EXAMPLES = \
    "Examples:\n"
    "  1. rmx_generic_send_demo_app --local-ip 1.2.3.4\n"
    "  2. rmx_generic_send_demo_app --local-ip 1.2.3.4 --dst-ip 231.1.2.3 --dst-port 60000";

/**
 * @brief Basic Rivermax API Generic Send Demo Application.
 *
 * This application demonstrates the most basic usage of the Rivermax Generic Send API functionality.
 */
class RmxGenericSendDemoApp : public RmxOutGenericAPIBaseDemoApp
{
public:
    RmxGenericSendDemoApp() : RmxOutGenericAPIBaseDemoApp(APP_DESCRIPTION, APP_EXAMPLES) {}
    virtual ~RmxGenericSendDemoApp() = default;
protected:
    virtual ReturnStatus operator()() override;
};

#endif // RMX_API_DEMO_APPS_RMX_OUTPUT_GENERIC_API_RMX_GENERIC_SEND_DEMO_APP_RMX_GENERIC_SEND_DEMO_APP_H_
