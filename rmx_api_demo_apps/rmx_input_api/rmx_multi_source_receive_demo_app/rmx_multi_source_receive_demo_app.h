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
#ifndef RMX_API_DEMO_APPS_RMX_INPUT_API_RMX_MULTI_SOURCE_RECEIVE_DEMO_APP_RMX_MULTI_SOURCE_RECEIVE_DEMO_APP_H_
#define RMX_API_DEMO_APPS_RMX_INPUT_API_RMX_MULTI_SOURCE_RECEIVE_DEMO_APP_RMX_MULTI_SOURCE_RECEIVE_DEMO_APP_H_

#include "rmx_input_api_base.h"
#include "api/rmax_apps_lib_api.h"

using namespace ral::lib::services;


constexpr const char* APP_DESCRIPTION = "NVIDIA Rivermax API Multiple Source-Specific Multicast Receive Demo App";
constexpr const char* APP_EXAMPLES = \
    "Examples:\n"
    "  1. rmx_multi_source_receive_demo_app --local-ip 1.2.3.4 --src-ips 1.2.10.1\n"
    "  2. rmx_multi_source_receive_demo_app --local-ip 1.2.3.4 --src-ips 1.2.10.1,1.2.10.2 --dst-ip 231.1.2.3 --dst-port 60000";

/**
 * @brief Rivermax Input API Multi Source Receive Demo Application.
 *
 * This application demonstrates how to use the Rivermax Receive API
 * functionality to add multiple source-specific flows to the same input
 * stream.
 */
class RmxMultiSSMReceiveDemoApp : public RmxInAPIBaseDemoApp
{
public:
    RmxMultiSSMReceiveDemoApp() : RmxInAPIBaseDemoApp(APP_DESCRIPTION, APP_EXAMPLES) {}
    virtual ~RmxMultiSSMReceiveDemoApp() = default;
protected:
    virtual ReturnStatus operator()() override;
    virtual void add_cli_options() override;
    virtual void post_cli_parse_initialization() override;
private:
    /* Source addresses */
    std::vector<sockaddr_in> m_source_address;
};

#endif // RMX_API_DEMO_APPS_RMX_INPUT_API_RMX_MULTI_SOURCE_RECEIVE_DEMO_APP_RMX_MULTI_SOURCE_RECEIVE_DEMO_APP_H_
