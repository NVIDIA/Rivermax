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
#include <cstring>
#include <iostream>

#ifdef __linux__
#include <netinet/in.h>
#else
#include <winsock2.h>
#endif

#include <rivermax_api.h>

#include "rmx_api_base_demo_app.h"
#include "api/rmax_apps_lib_api.h"

using namespace ral::lib::services;


constexpr uint16_t LOCAL_PORT_DEFAULT = 50000;

RmxAPIBaseDemoApp::RmxAPIBaseDemoApp(const std::string& app_description, const std::string& app_examples) :
    m_obj_init_status(ReturnStatus::obj_init_failure),
    m_app_settings(new AppSettings),
    m_rmax_apps_lib(ral::lib::RmaxAppsLibFacade()),
    m_cli_parser_manager(m_rmax_apps_lib.get_cli_parser_manager(
        app_description + rmx_get_version_string(), app_examples, m_app_settings)),
    m_signal_handler(m_rmax_apps_lib.get_signal_handler(true))
{
    std::memset(&m_local_address, 0, sizeof(m_local_address));
}

int RmxAPIBaseDemoApp::run(int argc, const char* argv[])
{
    m_obj_init_status = initialize(argc, argv);
    if (m_obj_init_status == ReturnStatus::obj_init_failure) {
        return EXIT_FAILURE;
    } else if (m_obj_init_status == ReturnStatus::success_cli_help) {
        return EXIT_SUCCESS;
    }

    try {
        ReturnStatus rc = operator()();
        if (rc != ReturnStatus::success) {
            std::cerr << "Application failed to run with status: " << static_cast<int>(rc) << std::endl;
            return EXIT_FAILURE;
        }
    } catch (const std::exception& error) {
        std::cerr << "Application failed to run with exception: " << error.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

void RmxAPIBaseDemoApp::add_cli_options()
{
    m_cli_parser_manager->add_option(CLIOptStr::LOCAL_IP);
}

void RmxAPIBaseDemoApp::initialize_common_default_app_settings()
{
    m_app_settings->destination_ip = DESTINATION_IP_DEFAULT;
    m_app_settings->destination_port = DESTINATION_PORT_DEFAULT;
}

ReturnStatus RmxAPIBaseDemoApp::initialize_local_address()
{
    std::memset(&m_local_address, 0, sizeof(sockaddr_in));
    m_local_address.sin_family = AF_INET;
    m_local_address.sin_port = htons(LOCAL_PORT_DEFAULT);
    int rc = inet_pton(AF_INET, m_app_settings->local_ip.c_str(), &m_local_address.sin_addr);
    if (rc != 1) {
        std::cerr << "Failed to convert local IP address" << std::endl;
        return ReturnStatus::failure;
    }

    return ReturnStatus::success;
}

ReturnStatus RmxAPIBaseDemoApp::initialize(int argc, const char* argv[])
{
    ReturnStatus rc = m_cli_parser_manager->initialize();
    if (rc != ReturnStatus::success) {
        std::cerr << "Failed to initialize CLI manager" << std::endl;
        return ReturnStatus::obj_init_failure;
    }

    initialize_common_default_app_settings();
    add_cli_options();
    rc = m_cli_parser_manager->parse_cli(argc, argv);
    if (rc == ReturnStatus::success_cli_help) {
        return ReturnStatus::success_cli_help;
    } else if (rc == ReturnStatus::failure) {
        std::cerr << "Failed to parse CLI options" << std::endl;
        return ReturnStatus::obj_init_failure;
    }
    post_cli_parse_initialization();

    rc = initialize_local_address();
    if (rc == ReturnStatus::failure) {
        std::cerr << "Failed to initialize NIC local address" << std::endl;
        return ReturnStatus::obj_init_failure;
    }

    return ReturnStatus::obj_init_success;
}
