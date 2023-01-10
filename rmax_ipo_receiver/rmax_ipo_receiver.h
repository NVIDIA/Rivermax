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
#include "apps/rmax_base_app.h"

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
    "  1. rmax_ipo_receiver --local-ip 1.2.3.4 --src-ip 6.7.8.9 --dst-ip 1.2.3.4 -p 50020 -v\n"
    "  2. rmax_ipo_receiver --local-ip 1.2.3.4,1.2.3.5 --src-ip 6.7.8.9,6.7.8.10 --dst-ip 1.2.3.4,1.2.3.5 -p 50020,50120 -v\n"
    "  3. rmax_ipo_receiver --local-ip 1.2.3.4,1.2.3.5 --src-ip 6.7.8.9,6.7.8.10 --dst-ip 1.2.3.4,1.2.3.5 -p 50020,50120 --app-hdr-size 50 -v\n"
    "  4. rmax_ipo_receiver --local-ip 1.2.3.4,1.2.3.5 --src-ip 6.7.8.9,6.7.8.10 --dst-ip 239.1.1.1,239.1.1.2 -p 50020,50120 -v\n"
    "  5. rmax_ipo_receiver --local-ip 1.2.3.4,1.2.3.5 --src-ip 6.7.8.9,6.7.8.10 --dst-ip 239.1.1.1,239.1.1.2 -p 50020,50120 --threads 2 --streams 10 -a 1,2 -i 3\n";

/**
 * @brief: IPO Receiver application.
 *
 * This is an example of usage application for Rivermax Inline Packet Ordering RX API.
 */
class IPOReceiverApp : public RmaxBaseApp
{
private:
    static constexpr uint32_t DEFAULT_NUM_OF_PACKETS_IN_CHUNK = 262144;
    static constexpr int USECS_IN_SECOND = 1000000;

    /* Sender objects container */
    std::vector<std::unique_ptr<IPOReceiverIONode>> m_receivers;
    /* Stream per thread distribution */
    std::unordered_map<size_t,size_t> m_streams_per_thread;
    /* Network recv flows */
    std::vector<std::vector<FourTupleFlow>> m_flows;
    // Maximum Path Differential for "Class B: Moderate-Skew" receivers defined
    // by SMPTE ST 2022-7:2019 "Seamless Protection Switching of RTP Datagrams".
    uint64_t m_max_path_differential_us = 50000;
    bool m_is_extended_sequence_number = false;
    byte_ptr_t m_header_buffer = nullptr;
    byte_ptr_t m_payload_buffer = nullptr;
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
    ReturnStatus run() override;
private:
    void add_cli_options() final;
    ReturnStatus initialize_connection_parameters() final;
    ReturnStatus initialize_rivermax_resources() final;
    /**
     * @brief: Initializes network receive streams.
     *
     * This method is responsible to initialize the receive streams will be
     * used in the application. Those streams will be distributed in @ref
     * ral::apps::IPOReceiverApp::distribute_work_for_threads to the streams
     * will be used in the application.
     * The application supports unicast and multicast UDPv4 receive flows.
     */
    void initialize_receive_streams();
    /**
     * @brief: Distributes work for threads.
     *
     * This method is responsible to distribute work to threads, by
     * distributing number of streams per receiver thread uniformly.
     * In future development, this can be extended to different
     * streams per thread distribution policies.
     */
    void distribute_work_for_threads();
    /**
     * @brief: Initializes receiver threads.
     *
     * This method is responsible to initialize
     * @ref ral::io_node::IPOReceiverIONode objects to work. It will initiate
     * objects with the relevant parameters. The objects initialized in this
     * method, will be the contexts to the std::thread objects will run in
     * @ref ral::apps::RmaxBaseApp::run_threads method.
     */
    void initialize_receive_threads();
    /**
     * @brief: Allocates application memory.
     *
     * This method is responsible to allocate the required memory for the application
     * using @ref ral::lib::services::MemoryAllocator interface.
     * The allocation policy of the application is allocating one big memory
     * block. This memory block will be distributed to the different
     * components of the application.
     *
     * @return: Return status of the operation.
     */
    ReturnStatus allocate_app_memory();
    /**
     * @brief: Distributes memory for receivers.
     *
     * This method is responsible to distribute the memory allocated
     * by @ref allocate_app_memory to the receivers of the application.
     */
    void distribute_memory_for_receivers();
    /**
     * @brief: Returns the required memory length for the application.
     *
     * This method is responsible to calculate the memory required for the application.
     * It will do so by iterating it's receivers and each receiver's streams.
     *
     * @param [out] hdr_mem_len: Required header memory length.
     * @param [out] pld_mem_len: Required payload memory length.
     *
     * @return: Return status of the operation.
     */
    ReturnStatus get_memory_length(size_t& hdr_mem_len, size_t& pld_mem_len);
    /**
     * @brief: Allocate memory and align it to page size.
     *
     * @param [in] size: Requested allocation size.
     *
     * @return: Pointer to allocated memory.
     */
    byte_ptr_t allocate_and_align(size_t size);
};

} // namespace rmax_xstream_media_sender
} // namespace apps
} // namespace ral

#endif /* RMAX_APPS_LIB_APPS_RMAX_IPO_RECEIVER_H_ */
