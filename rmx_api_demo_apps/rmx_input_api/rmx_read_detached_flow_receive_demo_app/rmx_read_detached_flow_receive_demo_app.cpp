/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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
#include <cstddef>
#include <cinttypes>
#include <thread>

#ifdef __linux__
#include <netinet/in.h>
#else
#include <winsock2.h>
#endif

#include <rivermax_api.h>

#include "rmx_read_detached_flow_receive_demo_app.h"
#include "api/rmax_apps_lib_api.h"
#include "rt_threads.h"

using namespace ral::lib::services;


RmxReadDetachedFlowDemoApp::RmxReadDetachedFlowDemoApp() :
    RmxAPIBaseDemoApp(APP_DESCRIPTION, APP_EXAMPLES)
{
    std::memset(&m_source_address, 0, sizeof(m_source_address));
}

void RmxReadDetachedFlowDemoApp::add_cli_options()
{
    RmxAPIBaseDemoApp::add_cli_options();

    m_cli_parser_manager->add_option(CLIOptStr::SRC_IP);
    m_cli_parser_manager->add_option(CLIOptStr::DST_IPS);
    m_cli_parser_manager->add_option(CLIOptStr::DST_PORTS);
}

void RmxReadDetachedFlowDemoApp::post_cli_parse_initialization()
{
    ReturnStatus status;

    RmxAPIBaseDemoApp::post_cli_parse_initialization();

    m_app_settings->packet_payload_size = DEFAULT_PACKET_PAYLOAD_SIZE;
    m_app_settings->num_of_packets_in_mem_block = DEFAULT_INPUT_BUFFER_SIZE;

    // Set remote address:
    status = initialize_address(m_app_settings->source_ip, m_app_settings->source_port, m_source_address);
    if (status != ReturnStatus::success) {
        std::cerr << "Invalid source IP address: " << m_app_settings->source_ip << std::endl;
        m_obj_init_status = ReturnStatus::obj_init_failure;
        return;
    }

    // Set destination addresses:
    if (m_app_settings->destination_ips.size() < 2) {
        std::cerr << "Must be at least two destination addresses" << std::endl;
        m_obj_init_status = ReturnStatus::obj_init_failure;
        return;
    }
    if (m_app_settings->destination_ips.size() != m_app_settings->destination_ports.size()) {
        std::cerr << "Must be equal number of destination addresses and ports" << std::endl;
        m_obj_init_status = ReturnStatus::obj_init_failure;
        return;
    }
    m_destination_address.resize(m_app_settings->destination_ips.size());
    for (size_t i = 0; i < m_app_settings->destination_ips.size(); ++i) {
        const auto& ip = m_app_settings->destination_ips[i];

        status = initialize_address(ip, m_app_settings->destination_ports[i], m_destination_address[i]);
        if (status != ReturnStatus::success) {
            std::cerr << "Invalid destination IP address: " << ip << std::endl;
            m_obj_init_status = ReturnStatus::obj_init_failure;
            return;
        }
    }
}

ReturnStatus RmxReadDetachedFlowDemoApp::receive_packets(rmx_stream_id stream_id, size_t sub_block_id, size_t data_stride_size_bytes)
{
    constexpr auto iteration_duration = std::chrono::seconds(5);

    /* Initialize chunk handle */
    rmx_input_chunk_handle chunk_handle;

    rmx_input_init_chunk_handle(&chunk_handle, stream_id);

    /* Data path loop */

    /**
     * @note Sleep for some time between chunks to avoid busy waiting.
     * We set this to 100[us] for this example.
     * This should be fine-tuned based on application requirements.
     */
    constexpr auto sleep_between_chunks = std::chrono::microseconds(100);
    const auto end_time = std::chrono::steady_clock::now() + iteration_duration;
    rmx_status status = RMX_OK;
    while (likely(status == RMX_OK && SignalHandler::get_received_signal() < 0 && std::chrono::steady_clock::now() < end_time)) {
        /** Get next chunk of received data **/
        status = rmx_input_get_next_chunk(&chunk_handle);
        EXIT_ON_FAILURE_WITH_CLEANUP(status, "Failed to get next chunk")

        /** Process the chunk **/
        const rmx_input_completion* completion = rmx_input_get_chunk_completion(&chunk_handle);
        EXIT_ON_CONDITION_WITH_CLEANUP(completion == nullptr, "Failed to get chunk completion")

        const uint8_t* chunk_ptr = reinterpret_cast<const uint8_t*>(rmx_input_get_completion_ptr(completion, sub_block_id));
        size_t chunk_size = rmx_input_get_completion_chunk_size(completion);

        for (size_t packet_index = 0; packet_index < chunk_size; packet_index++) {
            /*** Process the packet ***/
            const rmx_input_packet_info* packet_info = rmx_input_get_packet_info(&chunk_handle, packet_index);
            const size_t packet_payload_size = rmx_input_get_packet_size(packet_info, sub_block_id);
            const uint8_t* packet_payload_ptr = chunk_ptr + packet_index * data_stride_size_bytes;

            /**
             * @note Process the packet here.
             * For this example, we skip this step.
             */
            NOT_IN_USE(packet_payload_size);
            NOT_IN_USE(packet_payload_ptr);
        }

        std::this_thread::sleep_for(sleep_between_chunks);
    }
    return SignalHandler::get_received_signal() >= 0 ? ReturnStatus::signal_received : ReturnStatus::success;
}

ReturnStatus RmxReadDetachedFlowDemoApp::operator()()
{
    /* Initialize settings */

    /** Initialize app settings (@ref m_app_settings) **/
    // Done in @ref RmxReadDetachedFlowDemoApp::post_cli_parse_initialization, called from @ref RmxAPIBaseDemoApp::initialize.

    /** Initialize local address (@ref m_local_address) **/
    // Done in @ref RmxAPIBaseDemoApp::initialize_local_address, called from @ref RmxAPIBaseDemoApp::initialize.

    /* Initialize Rivermax library */
    rmx_status status = rmx_init();
    EXIT_ON_FAILURE(status, "Failed to initialize Rivermax library")

    /* Create stream */

    /** Set stream parameters **/
    rmx_input_stream_params stream_params;

    rmx_input_init_stream(&stream_params, RMX_INPUT_APP_PROTOCOL_PACKET);
    rmx_input_set_stream_nic_address(&stream_params, reinterpret_cast<sockaddr*>(&m_local_address));
    rmx_input_enable_stream_option(&stream_params, RMX_INPUT_STREAM_CREATE_INFO_PER_PACKET);

    /** Set memory layout **/
    constexpr size_t num_of_sub_blocks = 1; // Non-HDS mode
    constexpr size_t sub_block_id = 0;

    rmx_input_set_mem_capacity_in_packets(&stream_params, m_app_settings->num_of_packets_in_mem_block);
    rmx_input_set_mem_sub_block_count(&stream_params, num_of_sub_blocks);
    rmx_input_set_entry_uniform_size(&stream_params, sub_block_id, m_app_settings->packet_payload_size);

    status = rmx_input_determine_mem_layout(&stream_params);
    EXIT_ON_FAILURE_WITH_CLEANUP(status, "Failed to determine memory layout")

    size_t data_stride_size_bytes = rmx_input_get_stride_size(&stream_params, sub_block_id);

    /** Create the stream **/
    rmx_stream_id stream_id;
    status = rmx_input_create_stream(&stream_params, &stream_id);
    EXIT_ON_FAILURE_WITH_CLEANUP(status, "Failed to create stream")

    /**
     * Set completion moderation
     *
     * @note Completion moderation is a mechanism to control the rate at which completions are generated.
     * This can be used to reduce the number of completions generated, which can be useful in high-throughput scenarios.
     * This parameters should be fine-tuned based on application requirements.
     */
    constexpr size_t min_chunk_size = 0;
    constexpr size_t max_chunk_size = 5000;
    constexpr int wait_timeout_next_chunk = 0;

    status = rmx_input_set_completion_moderation(stream_id, min_chunk_size, max_chunk_size, wait_timeout_next_chunk);
    EXIT_ON_FAILURE_WITH_CLEANUP(status, "Failed to set completion moderation")

    /* Main loop */
    const size_t num_flows = m_destination_address.size();
    for (size_t flow_index = 0; flow_index < num_flows; ++flow_index) {
        if (flow_index > 0) {
            /** Receive packets from the input buffer where no flows are connected **/
            auto run_status = receive_packets(stream_id, sub_block_id, data_stride_size_bytes);
            if (run_status != ReturnStatus::success) {
                break;
            }
        }
        /** Attach network flow **/

        /*** Set flow parameters ***/
        rmx_input_flow receive_flow;

        rmx_input_init_flow(&receive_flow);
        rmx_input_set_flow_local_addr(&receive_flow, reinterpret_cast<sockaddr*>(&m_destination_address[flow_index]));
        rmx_input_set_flow_remote_addr(&receive_flow, reinterpret_cast<sockaddr*>(&m_source_address));
        rmx_input_set_flow_tag(&receive_flow, static_cast<uint32_t>(flow_index));

        /*** Attach the flow ***/
        status = rmx_input_attach_flow(stream_id, &receive_flow);
        EXIT_ON_FAILURE_WITH_CLEANUP(status, "Failed to attach flow")

        /** Receive packets from the input buffer while the flow is connected **/
        auto run_status = receive_packets(stream_id, sub_block_id, data_stride_size_bytes);
        if (run_status != ReturnStatus::success) {
            break;
        }

        /** Detach network flow **/
        status = rmx_input_detach_flow(stream_id, &receive_flow);
        EXIT_ON_FAILURE_WITH_CLEANUP(status, "Failed to detach flow")
    }

    /* Destroy stream */
    status = rmx_input_destroy_stream(stream_id);
    EXIT_ON_FAILURE_WITH_CLEANUP(status, "Failed to destroy stream")

    /* Clean-up Rivermax library */
    status = rmx_cleanup();
    EXIT_ON_FAILURE(status, "Failed to clean-up Rivermax library")

    return ReturnStatus::success;
}

int main(int argc, const char *argv[])
{
    return RmxReadDetachedFlowDemoApp().run(argc, argv);
}
