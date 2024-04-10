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
#ifndef RMX_API_DEMO_APPS_RMX_INPUT_MEDIA_API_rmx_input_api_base_H_
#define RMX_API_DEMO_APPS_RMX_INPUT_MEDIA_API_rmx_input_api_base_H_

#include <string>

#ifdef __linux__
#include <netinet/in.h>
#else
#include <winsock2.h>
#endif

#include "rmx_api_base_demo_app.h"


constexpr size_t DEFAULT_PACKET_PAYLOAD_SIZE = 1500;
constexpr size_t DEFAULT_INPUT_BUFFER_SIZE = 1 << 18;

/**
 * @brief Base class for all input API demo applications.
 *
 * This class provides the base functionality for all input API demo applications.
 * It provides common command line options and application initializations.
 */
class RmxInAPIBaseDemoApp : public RmxAPIBaseDemoApp
{
protected:
    RmxInAPIBaseDemoApp(const std::string& app_description, const std::string& app_examples);
    virtual ~RmxInAPIBaseDemoApp() = default;

    virtual void add_cli_options() override;
    virtual void post_cli_parse_initialization() override;

    /* Remote destination address */
    sockaddr_in m_destination_address;
};

#endif // RMX_API_DEMO_APPS_RMX_INPUT_MEDIA_API_rmx_input_api_base_H_
