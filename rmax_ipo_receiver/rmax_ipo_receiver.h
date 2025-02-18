/*
 * SPDX-FileCopyrightText: Copyright (c) 2022-23 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#ifndef RMAX_APPS_LIB_APPS_RMAX_IPO_RECEIVER_H_
#define RMAX_APPS_LIB_APPS_RMAX_IPO_RECEIVER_H_

#include <string>
#include <vector>
#include <memory>
#include <climits>
#include <unordered_set>

#include <rivermax_api.h>

#include "CLI/CLI.hpp"

#include "api/rmax_apps_lib_api.h"
#include "io_node/io_node.h"
#include "apps/rmax_receiver_base.h"

using namespace ral::lib::core;
using namespace ral::lib::services;
using namespace ral::io_node;

namespace ral
{
namespace apps
{
namespace rmax_ipo_receiver
{
/**
 * Application constants.
 */
constexpr const char* APP_DESCRIPTION = "NVIDIA Rivermax IPO receiver demo app ";
constexpr const char* APP_EXAMPLES = \
    "\nExamples:\n"
    "  1. rmax_ipo_receiver --local-ips 1.2.3.4 --src-ips 6.7.8.9 --dst-ips 1.2.3.4 -p 50020 -v\n"
    "  2. rmax_ipo_receiver --local-ips 1.2.3.4,1.2.3.5 --src-ips 6.7.8.9,6.7.8.10 --dst-ips 1.2.3.4,1.2.3.5 -p 50020,50120 -v\n"
    "  3. rmax_ipo_receiver --local-ips 1.2.3.4,1.2.3.5 --src-ips 6.7.8.9,6.7.8.10 --dst-ips 1.2.3.4,1.2.3.5 -p 50020,50120 --app-hdr-size 50 -v\n"
    "  4. rmax_ipo_receiver --local-ips 1.2.3.4,1.2.3.5 --src-ips 6.7.8.9,6.7.8.10 --dst-ips 239.1.1.1,239.1.1.2 -p 50020,50120 -v\n"
    "  5. rmax_ipo_receiver --local-ips 1.2.3.4,1.2.3.5 --src-ips 6.7.8.9,6.7.8.10 --dst-ips 239.1.1.1,239.1.1.2 -p 50020,50120 --threads 2 --streams 10 -a 1,2 -i 3\n";

/**
 * @brief: IPO Receiver application.
 *
 * This is an example of usage application for Rivermax Inline Packet Ordering RX API.
 */
class IPOReceiverApp : public RmaxReceiverBaseApp
{
private:
    static constexpr uint32_t DEFAULT_NUM_OF_PACKETS_IN_CHUNK = 262144;
    static constexpr int USECS_IN_SECOND = 1000000;

    /* Sender objects container */
    std::vector<std::unique_ptr<IPOReceiverIONode>> m_receivers;
    /* Network recv flows */
    std::vector<std::vector<FourTupleFlow>> m_flows;
    /* NIC device interfaces */
    std::vector<rmx_device_iface> m_device_ifaces;
    /* Memory regions for header memory allocated for each device interface */
    std::vector<rmx_mem_region> m_header_mem_regions;
    /* Memory regions for payload memory allocated for each device interface */
    std::vector<rmx_mem_region> m_payload_mem_regions;
    // Maximum Path Differential for "Class B: Moderate-Skew" receivers defined
    // by SMPTE ST 2022-7:2019 "Seamless Protection Switching of RTP Datagrams".
    uint64_t m_max_path_differential_us = 50000;
    bool m_is_extended_sequence_number = false;
    size_t m_num_paths_per_stream = 0;

public:
    /**
     * @brief: IPOReceiverApp class constructor.
     *
     * @param [in] argc: Number of CLI arguments.
     * @param [in] argv: CLI arguments strings array.
     */
    IPOReceiverApp(int argc, const char* argv[]);
    virtual ~IPOReceiverApp() = default;
private:
    void add_cli_options() final;
    ReturnStatus initialize_connection_parameters() final;
    /**
     * @brief: Initializes network receive flows.
     *
     * This method initializes the receive flows that will be used
     * in the application. These flows will be distributed
     * in @ref ral::apps::IPOReceiverApp::distribute_work_for_threads
     * between application threads.
     * The application supports unicast and multicast UDPv4 receive flows.
     */
    void configure_network_flows();
    /**
     * @brief: Initializes receiver I/O nodes.
     *
     * This method is responsible for initialization of
     * @ref ral::io_node::IPOReceiverIONode objects to work. It will initiate
     * objects with the relevant parameters. The objects initialized in this
     * method, will be the contexts to the std::thread objects will run in
     * @ref ral::apps::RmaxBaseApp::run_threads method.
     */
    void initialize_receive_io_nodes() final;
    /**
     * @brief: Registers previously allocated memory if requested.
     *
     * If @ref m_register_memory is set then this function registers
     * application memory using @ref rmax_register_memory.
     *
     * @return: Returns status of the operation.
     */
    ReturnStatus register_app_memory() final;
    /**
     * @brief: Unregister previously registered memory.
     *
     * Unregister memory using @ref rmax_deregister_memory.
     */
    void unregister_app_memory() final;
    /**
     * @brief: Distributes memory for receivers.
     *
     * This method is responsible for distributing the memory allocated
     * by @ref allocate_app_memory to the receivers of the application.
     */
    void distribute_memory_for_receivers() final;
    /**
     * @brief: Returns the memory size for all the receive streams.
     *
     * This method calculates the sum of memory sizes for all IONodes and their IPO streams.
     * Inside an IPO stream memory size is not summed along redundant streams,
     * they are only checked for equal requirements.
     *
     * @param [out] hdr_mem_size: Required header memory size.
     * @param [out] pld_mem_size: Required payload memory size.
     *
     * @return: Return status of the operation.
     */
    ReturnStatus get_total_streams_memory_size(size_t& hdr_mem_size, size_t& pld_mem_size) final;
    /**
     * @brief: Runs application threads.
     */
    void run_receiver_threads() final;
};

} // namespace rmax_xstream_media_sender
} // namespace apps
} // namespace ral

#endif /* RMAX_APPS_LIB_APPS_RMAX_IPO_RECEIVER_H_ */
