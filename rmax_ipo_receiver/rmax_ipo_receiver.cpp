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
    m_cli_parser_manager->add_option(CLIOptStr::ALLOCATOR_TYPE);
    m_cli_parser_manager->add_option(CLIOptStr::VERBOSE);

    parser->add_option("-D,--max-pd", m_max_path_differential_us, "Maximum path differential, us", true)
        ->check(CLI::Range(1, USECS_IN_SECOND));
    parser->add_flag("-X,--ext-seq-num", m_is_extended_sequence_number, "Parse extended sequence number from RTP payload");
    parser->add_flag("--register-memory", m_register_memory, "Register memory on application side for better performance");
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

    m_device_ifaces.resize(m_num_paths_per_stream);
    for (size_t i = 0; i < m_num_paths_per_stream; ++i) {
        in_addr device_address;
        if (inet_pton(AF_INET, m_app_settings->local_ips[i].c_str(), &device_address) != 1) {
            std::cerr << "Failed to parse address of device " << m_app_settings->local_ips[i] << std::endl;
            return ReturnStatus::failure;
        }
        rmx_status status = rmx_retrieve_device_iface_ipv4(&m_device_ifaces[i], &device_address);
        if (status != RMX_OK) {
            std::cerr << "Failed to get device: " << m_app_settings->local_ips[i] << " with status: " << status << std::endl;
            return ReturnStatus::failure;
        }
     }

    if (m_register_memory && m_app_settings->packet_app_header_size == 0) {
        std::cerr << "Memory registration is supported only in header-data split mode!" << std::endl;
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
        configure_network_flows();
        initialize_receive_io_nodes();
        ReturnStatus rc = allocate_app_memory();
        if (rc == ReturnStatus::failure) {
            std::cerr << "Failed to allocate the memory required for the application" << std::endl;
            return rc;
        }
        distribute_memory_for_receivers();
        run_threads(m_receivers);
        unregister_app_memory();
    }
    catch (const std::exception & error) {
        std::cerr << error.what() << std::endl;
        return ReturnStatus::failure;
    }

    return ReturnStatus::success;
}

ReturnStatus IPOReceiverApp::initialize_rivermax_resources()
{
    std::vector<int> cpu_affinity;

    if (m_app_settings->internal_thread_core != CPU_NONE) {
        cpu_affinity.push_back(m_app_settings->internal_thread_core);
    }

    rt_set_realtime_class();
    return m_rmax_apps_lib.initialize_rivermax(cpu_affinity);
}

void IPOReceiverApp::configure_network_flows()
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

void IPOReceiverApp::initialize_receive_io_nodes()
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

byte_t* IPOReceiverApp::allocate_and_align(size_t size)
{
    return static_cast<byte_t*>(m_mem_allocator->allocate_aligned(size, m_mem_allocator->get_page_size()));
}

ReturnStatus IPOReceiverApp::allocate_app_memory()
{
    size_t hdr_mem_size;
    size_t pld_mem_size;
    ReturnStatus rc = get_total_ipo_streams_memory_size(hdr_mem_size, pld_mem_size);
    if (rc != ReturnStatus::success) {
        return rc;
    }

    m_header_buffer = allocate_and_align(hdr_mem_size + pld_mem_size);
    m_payload_buffer = m_header_buffer + hdr_mem_size;

    if (m_header_buffer) {
        std::cout << "Allocated " << hdr_mem_size << " bytes for header"
            << " at address " << static_cast<void*>(m_header_buffer)
            << " and " <<  pld_mem_size << " bytes for payload"
            << " at address " << static_cast<void*>(m_payload_buffer) << std::endl;
    } else {
        std::cerr << "Failed to allocate RAM" << std::endl;
        return ReturnStatus::failure;
    }

    m_mem_regions.resize(m_num_paths_per_stream);

    if (m_register_memory) {
        for (size_t i = 0; i < m_num_paths_per_stream; ++i) {
            rmx_mem_reg_params mem_registry;
            rmx_init_mem_registry(&mem_registry, &m_device_ifaces[i]);
            m_mem_regions[i].addr = m_header_buffer;
            m_mem_regions[i].length = hdr_mem_size + pld_mem_size;
            rmx_status status = rmx_register_memory(&m_mem_regions[i], &mem_registry);
            if (status != RMX_OK) {
                std::cerr << "Failed to register payload memory with status: " << status << std::endl;
                return ReturnStatus::failure;
            }
        }
    }

    return ReturnStatus::success;
}

void IPOReceiverApp::unregister_app_memory()
{
    if (!m_register_memory) {
        return;
    }

    for (size_t i = 0; i < m_num_paths_per_stream; ++i) {
        rmx_status status = rmx_deregister_memory(&m_mem_regions[i], &m_device_ifaces[i]);
        if (status != RMX_OK) {
            std::cerr << "Failed to deregister RAM on device " << m_app_settings->local_ips[i] << " with status: " << status << std::endl;
        }
    }
}

ReturnStatus IPOReceiverApp::get_total_ipo_streams_memory_size(size_t& hdr_mem_size, size_t& pld_mem_size)
{
    hdr_mem_size = 0;
    pld_mem_size = 0;

    for (const auto& receiver : m_receivers) {
        for (const auto& stream : receiver->get_streams()) {
            size_t hdr_buf_size, pld_buf_size;
            ReturnStatus rc = stream->query_buffer_size(hdr_buf_size, pld_buf_size);
            if (rc != ReturnStatus::success) {
                std::cerr << "Failed to query buffer size for stream " << stream->get_id() << " of receiver " << receiver->get_index() << std::endl;
                return rc;
            }
            hdr_mem_size += m_mem_allocator->align_length(hdr_buf_size);
            pld_mem_size += m_mem_allocator->align_length(pld_buf_size);
        }
    }

    std::cout << "Application requires " << hdr_mem_size << " bytes of header memory and "
        << pld_mem_size << " bytes of payload memory" << std::endl;

    return ReturnStatus::success;
}

void IPOReceiverApp::distribute_memory_for_receivers()
{
    byte_t* hdr_ptr = m_header_buffer;
    byte_t* pld_ptr = m_payload_buffer;

    for (auto& receiver : m_receivers) {
        for (auto& stream : receiver->get_streams()) {
            size_t hdr, pld;

            stream->set_buffers(hdr_ptr, pld_ptr);
            if (m_register_memory) {
                stream->set_memory_keys(m_mem_regions, m_mem_regions);
            }
            stream->query_buffer_size(hdr, pld);

            hdr_ptr += m_mem_allocator->align_length(hdr);
            pld_ptr += m_mem_allocator->align_length(pld);
        }
    }
}
