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
#include <cstddef>
#include <cinttypes>
#include <thread>
#include <vector>

#ifdef __linux__
#include <netinet/in.h>
#else
#include <winsock2.h>
#endif

#include <rivermax_api.h>

#include "rmx_memory_registration_receive_demo_app.h"
#include "api/rmax_apps_lib_api.h"
#include "rt_threads.h"

using namespace ral::lib::services;


ReturnStatus RmxMemoryRegistrationReceiveDemoApp::operator()()
{
    /* Initialize settings */

    /** Initialize app settings (@ref m_app_settings) **/
    // Done in @ref RmxInAPIBaseDemoApp::post_cli_parse_initialization, called from @ref RmxAPIBaseDemoApp::initialize.

    /** Initialize local address (@ref m_local_address) **/
    // Done in @ref RmxAPIBaseDemoApp::initialize_local_address, called from @ref RmxAPIBaseDemoApp::initialize.

    /* Initialize Rivermax library */
    rmx_status status = rmx_init();
    EXIT_ON_FAILURE(status, "Failed to initialize Rivermax library")

    constexpr size_t num_of_streams = 2;

    /* Allocate memory */
    auto mem_allocator = m_rmax_apps_lib.get_memory_allocator(AllocatorType::Malloc, m_app_settings);
    auto mem_utils = mem_allocator->get_memory_utils();
    auto mem_alignment = get_cache_line_size();

    size_t total_size_in_bytes = num_of_streams * m_app_settings->num_of_packets_in_mem_block *
        m_app_settings->packet_payload_size;

    void* allocated_mem_ptr = mem_allocator->allocate_aligned(total_size_in_bytes, mem_alignment);
    EXIT_ON_CONDITION_WITH_CLEANUP(allocated_mem_ptr == nullptr, "Failed to allocate memory")
    auto rc = mem_utils->memory_set(allocated_mem_ptr, 0, total_size_in_bytes);
    EXIT_ON_CONDITION_WITH_CLEANUP(rc != ReturnStatus::success, "Failed to set memory")

    /* Register memory */

    /** Retrieve device interface **/
    rmx_device_iface device_interface;
    status = rmx_retrieve_device_iface_ipv4(&device_interface, &m_local_address.sin_addr);
    EXIT_ON_FAILURE(status, "Failed to get device")

    /** Initialize memory region **/
    rmx_mem_region app_mem_region;
    app_mem_region.addr = allocated_mem_ptr;
    app_mem_region.length = total_size_in_bytes;

    /** Initialize memory registry **/
    rmx_mem_reg_params mem_reg_params;
    rmx_init_mem_registry(&mem_reg_params, &device_interface);

    /** Register the memory **/
    status = rmx_register_memory(&app_mem_region, &mem_reg_params);
    EXIT_ON_FAILURE(status, "Failed to register application memory")
    rmx_mkey_id mkey = app_mem_region.mkey;

    /* Create streams */

    std::vector<rmx_input_stream_params> streams_params(num_of_streams);
    constexpr size_t num_of_sub_blocks = 1; // Non-HDS mode
    constexpr size_t sub_block_id = 0;

    for(auto& stream_params : streams_params) {
        /** Set streams parameters **/
        rmx_input_init_stream(&stream_params, RMX_INPUT_APP_PROTOCOL_PACKET);
        rmx_input_set_stream_nic_address(&stream_params, reinterpret_cast<sockaddr*>(&m_local_address));
        rmx_input_enable_stream_option(&stream_params, RMX_INPUT_STREAM_CREATE_INFO_PER_PACKET);

        /** Set memory layout **/
        rmx_input_set_mem_capacity_in_packets(&stream_params, m_app_settings->num_of_packets_in_mem_block);
        rmx_input_set_mem_sub_block_count(&stream_params, num_of_sub_blocks);
        rmx_input_set_entry_uniform_size(&stream_params, sub_block_id, m_app_settings->packet_payload_size);

        status = rmx_input_determine_mem_layout(&stream_params);
        EXIT_ON_FAILURE_WITH_CLEANUP(status, "Failed to determine memory layout")
    }

    size_t data_stride_size_bytes;
    char* app_mem_block = static_cast<char*>(allocated_mem_ptr);
    std::vector<rmx_mem_region*> streams_mem_buffers(num_of_streams);
    std::vector<rmx_stream_id> streams_id(num_of_streams);

    for (auto index = 0; index < num_of_streams; index++) {
        streams_mem_buffers[index] = rmx_input_get_mem_block_buffer(&streams_params[index], sub_block_id);
        data_stride_size_bytes = rmx_input_get_stride_size(&streams_params[index], sub_block_id);
        streams_mem_buffers[index]->addr = app_mem_block;
        streams_mem_buffers[index]->mkey = mkey;
        app_mem_block += streams_mem_buffers[index]->length;

        /** Create the stream **/
        status = rmx_input_create_stream(&streams_params[index], &streams_id[index]);
        EXIT_ON_FAILURE_WITH_CLEANUP(status, "Failed to create stream")
    }

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

    for (auto& stream_id : streams_id) {
        status = rmx_input_set_completion_moderation(stream_id, min_chunk_size, max_chunk_size, wait_timeout_next_chunk);
        EXIT_ON_FAILURE_WITH_CLEANUP(status, "Failed to set completion moderation")
    }

    /* Attach network flows */

    /** Set flows parameters **/
    std::vector<rmx_input_flow> receive_flows(num_of_streams);

    for (size_t index = 0; index < num_of_streams; index++) {
        rmx_input_init_flow(&receive_flows[index]);

        /**
         * @note Both flows are currently using the same destination address.
         * To receive flows with different attributes, modify the destination address
         * for each flow, setting a unique address for each.
         */
        rmx_input_set_flow_local_addr(&receive_flows[index], reinterpret_cast<sockaddr*>(&m_destination_address));

        /** Attach the flow **/
        status = rmx_input_attach_flow(streams_id[index], &receive_flows[index]);
        EXIT_ON_FAILURE_WITH_CLEANUP(status, "Failed to attach flow")
    }

    /* Initialize chunk handle */
    std::vector<rmx_input_chunk_handle> chunk_handles(num_of_streams);

    for (size_t index = 0; index < num_of_streams; index++) {
        rmx_input_init_chunk_handle(&chunk_handles[index], streams_id[index]);
    }

    /* Data path loop */

    /**
     * @note Sleep for some time between chunks to avoid busy waiting.
     * We set this to 300[us] for this example.
     * This should be fine-tuned based on application requirements.
     */
    constexpr auto sleep_between_chunks_us = std::chrono::microseconds(300);

    while (likely(status == RMX_OK && SignalHandler::get_received_signal() < 0)) {
        for (auto& chunk_handle : chunk_handles) {
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
        }

        std::this_thread::sleep_for(sleep_between_chunks_us);
    }

    for (size_t index = 0; index < num_of_streams; index++) {
        /* Detach network flow */
        status = rmx_input_detach_flow(streams_id[index], &receive_flows[index]);
        EXIT_ON_FAILURE_WITH_CLEANUP(status, "Failed to detach flow")

        /* Destroy stream */
        status = rmx_input_destroy_stream(streams_id[index]);
        EXIT_ON_FAILURE_WITH_CLEANUP(status, "Failed to destroy stream")
    }

    /* Deregister memory */
    status = rmx_deregister_memory(&app_mem_region, &device_interface);
    EXIT_ON_FAILURE(status, "Failed to deregister application memory")

    /* Clean-up Rivermax library */
    status = rmx_cleanup();
    EXIT_ON_FAILURE(status, "Failed to clean-up Rivermax library")

    return ReturnStatus::success;
}

int main(int argc, const char *argv[])
{
    return RmxMemoryRegistrationReceiveDemoApp().run(argc, argv);
}
