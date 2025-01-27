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
#ifndef RMX_API_DEMO_APPS_RMX_API_BASE_DEMO_APP_H_
#define RMX_API_DEMO_APPS_RMX_API_BASE_DEMO_APP_H_

#include <memory>
#include <string>
#include <iostream>

#ifdef __linux__
#include <netinet/in.h>
#else
#include <winsock2.h>
#endif

#include <rivermax_api.h>

#include "api/rmax_apps_lib_api.h"

using namespace ral::lib::services;


#define EXIT_ON_CONDITION_HELPER(condition, message, do_cleanup) \
    if (unlikely(condition)) {                                   \
        std::cerr << message << std::endl;                       \
        if (do_cleanup) {                                        \
            rmx_cleanup();                                       \
        }                                                        \
        return ReturnStatus::failure;                            \
    }
#define EXIT_ON_CONDITION(condition, message) EXIT_ON_CONDITION_HELPER(condition, message, false)
#define EXIT_ON_CONDITION_WITH_CLEANUP(condition, message) EXIT_ON_CONDITION_HELPER(condition, message, true)
#define EXIT_ON_FAILURE(status, message) EXIT_ON_CONDITION(                            \
    status != RMX_OK, std::string(message) + " with status: " + std::to_string(status) \
)
#define EXIT_ON_FAILURE_WITH_CLEANUP(status, message) EXIT_ON_CONDITION_WITH_CLEANUP(  \
    status != RMX_OK, std::string(message) + " with status: " + std::to_string(status) \
)

/**
 * @brief Base class for Rivermax API demo applications.
 *
 * This class provides a base class for Rivermax API demo applications.
 * It provides the basic functionality for CLI parsing, signal handling, and
 * application initialization. The user of this interface should override the
 * @ref RmxAPIBaseDemoApp::operator() method in order to implement the application
 * specific flow. All Rivermax related functionality should be done in the
 * @ref RmxAPIBaseDemoApp::operator() method.
 */
class RmxAPIBaseDemoApp
{
public:
    /**
     * @brief Runs the application.
     *
     * This is the main application entry point, it will call the @ref RmxAPIBaseDemoApp::initialize
     * and @ref RmxAPIBaseDemoApp::operator() methods.
     *
     * @param [in] argc: Number of CLI arguments.
     * @param [in] argv: CLI arguments strings array.
     *
     * @return EXIT_SUCCESS on success, EXIT_FAILURE on failure.
     */
    int run(int argc, const char** argv);
protected:
    /**
     * @brief RmxAPIBaseDemoApp constructor.
     *
     * @param [in] app_description: Application description string for the CLI usage.
     * @param [in] app_examples: Application examples string for the CLI usage.
     */
    RmxAPIBaseDemoApp(const std::string& app_description, const std::string& app_examples);
    virtual ~RmxAPIBaseDemoApp() = default;
    /**
     * @brief Application main flow.
     *
     * This is the main application flow, the user of this interface should override this method
     * in order to implement the application specific flow. All Rivermax related functionality
     * should be done in this method.
     *
     * @return Status of the operation.
     */
    virtual ReturnStatus operator()() = 0;
    /**
     * @brief Adds CLI options and/or arguments to the parser.
     *
     * Use this method to add CLI options to the application, by using @ref m_cli_parser_manager->add_option.
     * It will be called as part of the @ref RmxAPIBaseDemoApp::initialize process.
     */
    virtual void add_cli_options();
    /**
     * @brief Initializes application common default settings.
     *
     * The user of this interface can override function, in order to override
     * application specific default setting.
     * It will be called as part of the @ref RmxAPIBaseDemoApp::initialize process.
     */
    virtual void initialize_common_default_app_settings();
    /**
     * @brief Does post CLI parsing initialization.
     *
     * Use this method to do any needed post CLI parsing application initialization.
     * It will be called as part of the @ref RmxAPIBaseDemoApp::initialize process.
     */
    virtual void post_cli_parse_initialization(){};
    /**
     * @brief Initializes socket address.
     *
     * This method initializes a socket address structure with the given IP and port.
     *
     * @param [in] ip: IP address string.
     * @param [in] port: Port number.
     * @param [out] address: Socket address structure to initialize.
     *
     * @return Status of the operation, on success: @ref ral::lib:services::ReturnStatus::success.
     */
    static ReturnStatus initialize_address(const std::string& ip, uint16_t port, sockaddr_in& address);
private:
    /**
     * @brief Runs application initialization flow.
     *
     * This method runs application initialization flow using the other methods in this
     * interface.
     *
     * The user can override the proposed initialization flow.
     *
     * @param [in] argc: Number of CLI arguments.
     * @param [in] argv: CLI arguments strings array.
     *
     * @return Status of the operation, on success: @ref ral::lib:services::ReturnStatus::obj_init_success.
     */
    ReturnStatus initialize(int argc, const char** argv);
protected:
    /* Indicator on whether the object created correctly */
    ReturnStatus m_obj_init_status;
    /* Application settings */
    std::shared_ptr<AppSettings> m_app_settings;
    /* Rivermax applications library facade */
    ral::lib::RmaxAppsLibFacade m_rmax_apps_lib;
    /* Command line manager */
    std::shared_ptr<CLIParserManager> m_cli_parser_manager;
    /* Application signal handler */
    std::shared_ptr<SignalHandler> m_signal_handler;
    /* Local NIC address */
    sockaddr_in m_local_address;
};

#endif // RMX_API_DEMO_APPS_RMX_API_BASE_DEMO_APP_H_
