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
#include <vector>

#ifdef __linux__
#include <netinet/in.h>
#else
#include <winsock2.h>
#endif

#include <rivermax_api.h>

#include "rmx_generic_send_demo_app.h"
#include "api/rmax_apps_lib_api.h"
#include "rt_threads.h"

using namespace ral::lib::services;


ReturnStatus RmxGenericSendDemoApp::operator()()
{
    /* Initialize settings */

    /** Initialize app settings (@ref m_app_settings) **/
    // Done in @ref RmxOutGenericAPIBaseDemoApp::post_cli_parse_initialization, called from @ref RmxAPIBaseDemoApp::initialize.

    /** Initialize local address (@ref m_local_address) **/
    // Done in @ref RmxAPIBaseDemoApp::initialize_local_address, called from @ref RmxAPIBaseDemoApp::initialize.

    /* Initialize Rivermax library */
    rmx_status status = rmx_init();
    EXIT_ON_FAILURE(status, "Failed to initialize Rivermax library");

    /* Allocate memory */
    auto mem_allocator = m_rmax_apps_lib.get_memory_allocator(AllocatorType::Malloc, m_app_settings);
    auto mem_utils = mem_allocator->get_memory_utils();
    auto mem_alignment = get_cache_line_size();

    size_t total_size_in_bytes = m_app_settings->num_of_chunks *
        m_app_settings->num_of_packets_in_chunk * m_app_settings->packet_payload_size;

    void* allocated_mem_ptr = mem_allocator->allocate_aligned(total_size_in_bytes, mem_alignment);
    EXIT_ON_CONDITION_WITH_CLEANUP(allocated_mem_ptr == nullptr, "Failed to allocate memory");
    auto rc = mem_utils->memory_set(allocated_mem_ptr, 0, total_size_in_bytes);
    EXIT_ON_CONDITION_WITH_CLEANUP(rc != ReturnStatus::success, "Failed to set memory");

    /* Register memory */

    /** Retrieve device interface **/
    rmx_device_iface device_interface;
    status = rmx_retrieve_device_iface_ipv4(&device_interface, &m_local_address.sin_addr);
    EXIT_ON_FAILURE(status, "Failed to get device");

    /** Initialize memory region **/
    rmx_mem_region app_mem_region;
    app_mem_region.addr = allocated_mem_ptr;
    app_mem_region.length = total_size_in_bytes;

    /** Initialize memory registry **/
    rmx_mem_reg_params mem_reg_params;
    rmx_init_mem_registry(&mem_reg_params, &device_interface);

    /** Register the memory **/
    status = rmx_register_memory(&app_mem_region, &mem_reg_params);
    EXIT_ON_FAILURE(status, "Failed to register application memory");
    rmx_mkey_id mkey = app_mem_region.mkey;

    /* Set memory layout */
    std::vector<std::vector<rmx_mem_region>> chunks(m_app_settings->num_of_chunks);
    char* curr_packet_addr = static_cast<char*>(allocated_mem_ptr);

    for (auto& chunk : chunks) {
        chunk = std::vector<rmx_mem_region>(m_app_settings->num_of_packets_in_chunk);
        /* Set memory region */
        for (auto& packet : chunk) {
            packet.addr = curr_packet_addr;
            packet.length = m_app_settings->packet_payload_size;
            packet.mkey = mkey;
            curr_packet_addr += packet.length;
        }
    }

    /* Create generic stream */

    /** Set stream parameters **/
    rmx_output_gen_stream_params stream_params;

    rmx_output_gen_init_stream(&stream_params);
    rmx_output_gen_set_local_addr(&stream_params, reinterpret_cast<sockaddr*>(&m_local_address));
    rmx_output_gen_set_remote_addr(&stream_params, reinterpret_cast<sockaddr*>(&m_destination_address));
    rmx_output_gen_set_packets_per_chunk(&stream_params, m_app_settings->num_of_packets_in_chunk);

    /** Create the stream **/
    rmx_stream_id stream_id;
    status = rmx_output_gen_create_stream(&stream_params, &stream_id);
    EXIT_ON_FAILURE_WITH_CLEANUP(status, "Failed to create stream");

    /* Initialize chunk handle */
    rmx_output_gen_chunk_handle chunk_handle;
    rmx_output_gen_init_chunk_handle(&chunk_handle, stream_id);

    /* Data path loop */

    uint64_t commit_timestamp_ns = 0;
    uint16_t payload_size = 0;
    uint8_t* payload_ptr = nullptr;

     while (likely(status == RMX_OK && SignalHandler::get_received_signal() < 0)) {
        for (auto& chunk : chunks) {
            /** Get the next chunk to send **/
            do {
                status = rmx_output_gen_get_next_chunk(&chunk_handle);
            } while (unlikely(status == RMX_NO_FREE_CHUNK));
            EXIT_ON_FAILURE_WITH_CLEANUP(status, "Failed to get next chunk");

            /** Prepare chunk's data **/
            for (auto& packet : chunk) {
                payload_size = packet.length;
                payload_ptr = reinterpret_cast<uint8_t*>(packet.addr);

                /**
                 * @note Fill the packet with payload.
                 * For this example, we skip this step.
                 */
                NOT_IN_USE(payload_size);
                NOT_IN_USE(payload_ptr);

                status = rmx_output_gen_append_packet_to_chunk(&chunk_handle, &packet, 1);
                EXIT_ON_FAILURE_WITH_CLEANUP(status, "Failed to append packet to chunk");
            }

            /** Commit the chunk ***/
            do {
                status = rmx_output_gen_commit_chunk(&chunk_handle, commit_timestamp_ns);
            } while (unlikely(status == RMX_HW_SEND_QUEUE_IS_FULL));
            EXIT_ON_FAILURE_WITH_CLEANUP(status, "Failed to commit chunk");
        }
    }

    /* Destroy generic stream */
    do {
        status = rmx_output_gen_destroy_stream(stream_id);
    } while (status == RMX_BUSY);
    EXIT_ON_FAILURE_WITH_CLEANUP(status, "Failed to destroy stream");

    /* Deregister memory */
    status = rmx_deregister_memory(&app_mem_region, &device_interface);
    EXIT_ON_FAILURE(status, "Failed to deregister application memory");

    /* Clean-up Rivermax library */
    status = rmx_cleanup();
    EXIT_ON_FAILURE(status, "Failed to clean-up Rivermax library");

    return ReturnStatus::success;
}

int main(int argc, const char *argv[])
{
    return RmxGenericSendDemoApp().run(argc, argv);
}
