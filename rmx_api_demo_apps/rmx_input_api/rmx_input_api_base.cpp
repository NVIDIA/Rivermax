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
#include <cstring>
#include <iostream>

#ifdef __linux__
#include <netinet/in.h>
#else
#include <winsock2.h>
#endif

#include "rmx_input_api_base.h"
#include "api/rmax_apps_lib_api.h"

using namespace ral::lib::services;


RmxInAPIBaseDemoApp::RmxInAPIBaseDemoApp(const std::string& app_description, const std::string& app_examples) :
    RmxAPIBaseDemoApp(app_description, app_examples)
{
    std::memset(&m_destination_address, 0, sizeof(m_destination_address));
}

void RmxInAPIBaseDemoApp::add_cli_options()
{
    RmxAPIBaseDemoApp::add_cli_options();

    m_cli_parser_manager->add_option(CLIOptStr::DST_IP);
    m_cli_parser_manager->add_option(CLIOptStr::DST_PORT);
}

void RmxInAPIBaseDemoApp::post_cli_parse_initialization()
{
    auto& s = m_app_settings;

    s->packet_payload_size = DEFAULT_PACKET_PAYLOAD_SIZE;
    s->num_of_packets_in_mem_block = DEFAULT_INPUT_BUFFER_SIZE;

    // Set remote address:
    auto rc = initialize_address(s->destination_ip, s->destination_port, m_destination_address);
    if (rc != ReturnStatus::success) {
        m_obj_init_status = ReturnStatus::obj_init_failure;
        return;
    }
}
