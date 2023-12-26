/*
 * SPDX-FileCopyrightText: Copyright (c) 2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include <stdlib.h>
#include <string.h>
#include <iostream>
#include <chrono>
#include <thread>
#include <rivermax_api.h>
#include "CLI/CLI.hpp"
#include "rt_threads.h"
#ifdef __linux__
#include <arpa/inet.h>
#else
#include <Ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#endif
#include "generic_receiver.h"

static void do_cleanup_exit()
{
    rmax_cleanup();
    exit(-1);
}

int main(int argc, char *argv[])
{
    CLI::App app{"Mellanox Rivermax Generic RX Demo App"};

    std::string local_ip;
    std::string dst_ip;
    std::string src_ip;
    uint16_t port;
    uint16_t header_size = 0;
    uint16_t payload_size = 1500;
    uint32_t buffer_elements = 1 << 16;
    uint16_t flow_id = 1;
    int gpu = GPU_ID_INVALID;
    bool use_checksum_header = false;
    std::vector<int> cpu_affinity;

    app.add_option("-i,--interface-ip", local_ip, "IP of the local interface")->required()->check(CLI::ValidIPV4);
    app.add_option("-m,--dst-address", dst_ip, "Destination address to bind to")->required()->check(CLI::ValidIPV4);
    app.add_option("-s,--src-address", src_ip, "Source address to read from")->required()->check(CLI::ValidIPV4);
    app.add_option("-p,--port", port, "Receive port to use")->required()->check(CLI::Range(1, 65535));
    auto *opt_checksum = app.add_flag("-x,--checksum-header", use_checksum_header, "Use checksum header");
    app.add_option("-r,--header-size", header_size, "User header size", true)->check(CLI::PositiveNumber)->excludes(opt_checksum);
    app.add_option("-d,--data-size", payload_size, "User data (payload) size", true)->check(CLI::PositiveNumber);
    app.add_option("-k,--packets", buffer_elements, "Number of packets to allocate memory for", true)->check(CLI::PositiveNumber);
    app.add_option("-f,--flow-id", flow_id, "Flow id to use", true)->check(CLI::PositiveNumber);
#ifdef CUDA_ENABLED
    app.add_option("-g,--gpu", gpu, "GPU to use for GPUDirect (default doesn't use GPU)", true);
#endif
    app.add_option("-a,--cpu-affinity", cpu_affinity,
        "Comma separated list of CPU affinity cores for the application main thread."
        )->delimiter(',')->check(CLI::Range(CPU_NONE, MAX_CPU_RANGE));

    CLI11_PARSE(app, argc, argv);

    // Print the library and app version.
    const char *rmax_version = rmax_get_version_string();
    static std::string app_version =
        std::to_string(RMAX_MAJOR_VERSION) + std::string(".") +
        std::to_string(RMAX_MINOR_VERSION) + std::string(".") +
        std::to_string(RMAX_PATCH_VERSION);
    std::cout << "#########################################\n";
    std::cout << "## Rivermax SDK version:        " << rmax_version << "\n";
    std::cout << "## Application version:         " << app_version << "\n";
    std::cout << "#########################################\n";

    // Initializes signals caught by the application
    initialize_signals();

    // Initialize Rivermax library.
    rmax_init_config init_config;
    memset(&init_config, 0, sizeof(init_config));
    init_config.flags |= RIVERMAX_HANDLE_SIGNAL;
    rmax_status_t rmax_status = rmax_init(&init_config);
    if (rmax_status != RMAX_OK) {
        std::cerr << "Failed initializing Rivermax; error: " << rmax_status << std::endl;
        exit(-1);
    }

    // Create stream.
    sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_port = htons((uint16_t)port);
    int rc = inet_pton(AF_INET, local_ip.c_str(), &local_addr.sin_addr);
    if (rc != 1) {
        std::cerr << "Failed to parse local network address" << std::endl;
        do_cleanup_exit();
    }

    bool status = false;

    gpu_init_config gp_init_config;
    memset(&gp_init_config, 0, sizeof(gp_init_config));
    if (gpu != GPU_ID_INVALID) {

        status = gpu_init(gpu, gp_init_config);
        if (!status) {
            do_cleanup_exit();
        }
    }

    RxStream* p_stream = new RxStream(RMAX_APP_PROTOCOL_PACKET, RMAX_IN_BUFFER_ATTER_FLAG_NONE,
                                      RMAX_PACKET_TIMESTAMP_RAW_NANO,
                                      buffer_elements, payload_size, header_size,
                                      local_addr, gpu, use_checksum_header, cpu_affinity);
    if (!p_stream ) {
        std::cerr << "Failed to create stream." << std::endl;
        do_cleanup_exit();
    }

    status = p_stream->stream_initialize();
    if (!status) {
        std::cerr << "Failed initializing stream." << std::endl;
        delete p_stream;
        do_cleanup_exit();
    }

    // Prepare flow attributes.
    rmax_in_flow_attr in_flow;
    memset(&in_flow, 0, sizeof(in_flow));
    in_flow.local_addr.sin_port = htons((uint16_t)port);
    in_flow.local_addr.sin_family = AF_INET;
    rc = inet_pton(AF_INET, dst_ip.c_str(), &in_flow.local_addr.sin_addr);
    if (rc != 1) {
        std::cerr << "Failed to parse destination network address" << std::endl;
        delete p_stream;
        do_cleanup_exit();
    }
    in_flow.remote_addr.sin_family = AF_INET;
    rc = inet_pton(AF_INET, src_ip.c_str(), &in_flow.remote_addr.sin_addr);
    if (rc != 1) {
        std::cerr << "Failed to parse source network address" << std::endl;
        delete p_stream;
        do_cleanup_exit();
    }

    // Attach flow to stream.
    in_flow.flow_id = flow_id;
    if (!p_stream->attach_flow(in_flow)) {
        std::cerr << "Failed to attach flow to stream." << std::endl;
        delete p_stream;
        do_cleanup_exit();
    }

    // Run the main loop.
    std::cout << "Running main receive loop..." << std::endl;
    status = p_stream->main_loop();
    if (!status) {
        std::cerr << "Failure in main receive loop." << std::endl;
        delete p_stream;
        do_cleanup_exit();
    } else {
        std::cout << "Main receive loop completed; exiting." << std::endl;
    }

    if (gpu != GPU_ID_INVALID) {
        gpu_uninit(gpu, gp_init_config);
    }

    // Cleanup.
    delete p_stream;
    rmax_cleanup();

    return 0;
}

