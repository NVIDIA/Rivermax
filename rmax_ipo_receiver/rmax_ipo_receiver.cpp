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

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <string>

#include <rivermax_api.h>

#include "rt_threads.h"
#include "rmax_ipo_receiver.h"
#include "api/rmax_apps_lib_api.h"
#include "io_node/io_node.h"
#include "apps/rmax_base_app.h"

using namespace ral::lib::core;
using namespace ral::lib::services;
using namespace ral::io_node;
using namespace ral::apps::rmax_ipo_receiver;


int main(int argc, const char* argv[])
{
    IPOReceiverApp app(argc, argv);

    ReturnStatus rc = app.run();
    if (rc == ReturnStatus::failure) {
        std::cerr << "Rivermax IPO Receiver failed to run" << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

IPOReceiverApp::IPOReceiverApp(int argc, const char* argv[]) :
    RmaxBaseApp(APP_DESCRIPTION, APP_EXAMPLES)
{
    m_obj_init_status = initialize(argc, argv);
}

void IPOReceiverApp::add_cli_options()
{
    // set application-specific default values
    m_app_settings->num_of_packets_in_chunk = DEFAULT_NUM_OF_PACKETS_IN_CHUNK;

    // set CLI options
    std::shared_ptr<CLI::App> parser = m_cli_parser_manager->get_parser();

    m_cli_parser_manager->add_option(CLIOptStr::SRC_IPS);
    m_cli_parser_manager->add_option(CLIOptStr::DST_IPS);
    m_cli_parser_manager->add_option(CLIOptStr::LOCAL_IPS);
    m_cli_parser_manager->add_option(CLIOptStr::DST_PORTS);
    m_cli_parser_manager->add_option(CLIOptStr::THREADS);
    m_cli_parser_manager->add_option(CLIOptStr::STREAMS)
        ->check(StreamToThreadsValidator(m_app_settings->num_of_threads));
    m_cli_parser_manager->add_option(CLIOptStr::PACKETS);
    m_cli_parser_manager->add_option(CLIOptStr::PAYLOAD_SIZE);
    m_cli_parser_manager->add_option(CLIOptStr::APP_HDR_SIZE);
    m_cli_parser_manager->add_option(CLIOptStr::INTERNAL_CORE);
    m_cli_parser_manager->add_option(CLIOptStr::APPLICATION_CORE);
    m_cli_parser_manager->add_option(CLIOptStr::SLEEP_US);
    m_cli_parser_manager->add_option(CLIOptStr::VERBOSE);

    parser->add_option("-D,--max-pd", m_max_path_differential_us, "Maximum path differential, us", true)
        ->check(CLI::Range(1, USECS_IN_SECOND));
    parser->add_flag("-X,--ext-seq-num", m_is_extended_sequence_number, "Parse extended sequence number from RTP payload");
}

ReturnStatus IPOReceiverApp::initialize_connection_parameters()
{
    m_num_paths_per_stream = m_app_settings->source_ips.size();
    if (m_num_paths_per_stream == 0) {
        std::cerr << "Must be at least one source IP" << std::endl;
        return ReturnStatus::failure;
    }
    if (m_app_settings->destination_ips.size() != m_num_paths_per_stream) {
        std::cerr << "Must be the same number of destination multicast IPs as number of source IPs" << std::endl;
        return ReturnStatus::failure;
    }
    if (m_app_settings->local_ips.size() != m_num_paths_per_stream) {
        std::cerr << "Must be the same number of NIC addresses as number of source IPs" << std::endl;
        return ReturnStatus::failure;
    }
    if (m_app_settings->destination_ports.size() != m_num_paths_per_stream) {
        std::cerr << "Must be the same number of destination ports as number of source IPs" << std::endl;
        return ReturnStatus::failure;
    }

    return ReturnStatus::success;
}

ReturnStatus IPOReceiverApp::run()
{
    if (m_obj_init_status != ReturnStatus::obj_init_success) {
        return m_obj_init_status;
    }

    try {
        distribute_work_for_threads();
        initialize_receive_streams();
        initialize_receive_threads();
        ReturnStatus rc = allocate_app_memory();
        if (rc == ReturnStatus::failure) {
            std::cerr << "Failed to allocate the memory required for the application" << std::endl;
            return rc;
        }
        distribute_memory_for_receivers();
        run_threads(m_receivers);
    }
    catch (const std::exception & error) {
        std::cerr << error.what() << std::endl;
        return ReturnStatus::failure;
    }

    return ReturnStatus::success;
}

ReturnStatus IPOReceiverApp::initialize_rivermax_resources()
{
    rmax_init_config init_config;
    memset(&init_config, 0, sizeof(init_config));

    init_config.flags |= RIVERMAX_HANDLE_SIGNAL;

    if (m_app_settings->internal_thread_core != CPU_NONE) {
        RMAX_CPU_SET(m_app_settings->internal_thread_core, &init_config.cpu_mask);
        init_config.flags |= RIVERMAX_CPU_MASK;
    }
    rt_set_realtime_class();
    return m_rmax_apps_lib.initialize_rivermax(init_config);
}

void IPOReceiverApp::initialize_receive_streams()
{
    std::vector<std::string> ip_prefix_str;
    std::vector<int> ip_last_octet;
    uint16_t src_port = 0;

    assert(m_num_paths_per_stream > 0);
    ip_prefix_str.resize(m_num_paths_per_stream);
    ip_last_octet.resize(m_num_paths_per_stream);
    for (size_t i = 0; i < m_num_paths_per_stream; ++i) {
        auto ip_vec = CLI::detail::split(m_app_settings->destination_ips[i], '.');
        ip_prefix_str[i] = std::string(ip_vec[0] + "." + ip_vec[1] + "." + ip_vec[2] + ".");
        ip_last_octet[i] = std::stoi(ip_vec[3]);
    }

    m_flows.reserve(m_app_settings->num_of_total_streams);
    size_t id = 0;
    for (size_t flow_index = 0; flow_index < m_app_settings->num_of_total_streams; ++flow_index) {
        std::vector<FourTupleFlow> paths;
        for (size_t i = 0; i < m_num_paths_per_stream; ++i) {
            std::ostringstream ip;
            ip << ip_prefix_str[i] << (ip_last_octet[i] + flow_index * m_num_paths_per_stream) % IP_OCTET_LEN;
            paths.emplace_back(id++, m_app_settings->source_ips[i], src_port, ip.str(), m_app_settings->destination_ports[i]);
        }
        m_flows.push_back(paths);
    }
}

void IPOReceiverApp::distribute_work_for_threads()
{
    m_streams_per_thread.reserve(m_app_settings->num_of_threads);
    for (int stream = 0; stream < m_app_settings->num_of_total_streams; stream++) {
        m_streams_per_thread[stream % m_app_settings->num_of_threads]++;
    }
}

void IPOReceiverApp::initialize_receive_threads()
{
    size_t streams_offset = 0;
    for (size_t rx_idx = 0; rx_idx < m_app_settings->num_of_threads; rx_idx++) {
        int recv_cpu_core = m_app_settings->app_threads_cores[rx_idx % m_app_settings->app_threads_cores.size()];

        auto flows = std::vector<std::vector<FourTupleFlow>>(
            m_flows.begin() + streams_offset,
            m_flows.begin() + streams_offset + m_streams_per_thread[rx_idx]);
        m_receivers.push_back(std::unique_ptr<IPOReceiverIONode>(new IPOReceiverIONode(
            *m_app_settings,
            m_max_path_differential_us,
            m_is_extended_sequence_number,
            m_app_settings->local_ips,
            rx_idx,
            recv_cpu_core)));

        m_receivers[rx_idx]->initialize_streams(streams_offset, flows);
        streams_offset += m_streams_per_thread[rx_idx];
    }
}

byte_ptr_t IPOReceiverApp::allocate_and_align(size_t size)
{
    return static_cast<byte_ptr_t>(m_mem_allocator->allocate_aligned(size, m_mem_allocator->get_page_size()));
}

ReturnStatus IPOReceiverApp::allocate_app_memory()
{
    size_t hdr_mem_len;
    size_t pld_mem_len;
    ReturnStatus rc = get_memory_length(hdr_mem_len, pld_mem_len);
    if (rc != ReturnStatus::success) {
        return rc;
    }

    if (hdr_mem_len) {
        m_header_buffer = allocate_and_align(hdr_mem_len);

        if (m_header_buffer) {
            std::cout << "Allocated " << hdr_mem_len << " header bytes at address "
                << static_cast<void*>(m_header_buffer) << std::endl;
        } else {
            std::cerr << "Failed to allocate header memory" << std::endl;
            return ReturnStatus::failure;
        }
    }
    m_payload_buffer = allocate_and_align(pld_mem_len);

    if (m_payload_buffer) {
        std::cout << "Allocated " << pld_mem_len << " payload bytes at address "
            << static_cast<void*>(m_payload_buffer) << std::endl;
    } else {
        std::cerr << "Failed to allocate payload memory" << std::endl;
        return ReturnStatus::failure;
    }

    return ReturnStatus::success;
}

ReturnStatus IPOReceiverApp::get_memory_length(size_t& hdr_mem_len, size_t& pld_mem_len)
{
    hdr_mem_len = 0;
    pld_mem_len = 0;

    for (const auto& receiver : m_receivers) {
        for (const auto& stream : receiver->get_streams()) {
            size_t hdr, pld;
            ReturnStatus rc = stream->query_buffer_size(hdr, pld);
            if (rc != ReturnStatus::success) {
                std::cerr << "Failed to query buffer size for stream " << stream->get_id() << " of receiver " << receiver->get_index() << std::endl;
                return rc;
            }
            hdr_mem_len += m_mem_allocator->align_length(hdr);
            pld_mem_len += m_mem_allocator->align_length(pld);
        }
    }

    std::cout << "Application requires " << hdr_mem_len << " bytes of header memory and "
        << pld_mem_len << " bytes of payload memory" << std::endl;

    return ReturnStatus::success;
}

void IPOReceiverApp::distribute_memory_for_receivers()
{
    byte_ptr_t hdr_ptr = m_header_buffer;
    byte_ptr_t pld_ptr = m_payload_buffer;

    for (auto& receiver : m_receivers) {
        for (auto& stream : receiver->get_streams()) {
            size_t hdr, pld;

            stream->set_buffers(hdr_ptr, pld_ptr);
            stream->query_buffer_size(hdr, pld);

            hdr_ptr += m_mem_allocator->align_length(hdr);
            pld_ptr += m_mem_allocator->align_length(pld);
        }
    }
}
