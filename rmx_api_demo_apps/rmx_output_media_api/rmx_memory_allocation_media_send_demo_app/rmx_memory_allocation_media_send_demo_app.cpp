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
#include <chrono>
#include <vector>

#ifdef __linux__
#include <netinet/in.h>
#else
#include <winsock2.h>
#endif

#include <rivermax_api.h>

#include "rmx_memory_allocation_media_send_demo_app.h"
#include "api/rmax_apps_lib_api.h"
#include "rt_threads.h"

using namespace ral::lib::services;


ReturnStatus RmxMemoryAllocationMediaSendDemoApp::operator()()
{
    /* Initialize settings */

    /** Initialize app settings (@ref m_app_settings) **/
    // Done in @ref RmxOutMediaAPIBaseDemoApp::post_cli_parse_initialization, called from @ref RmxAPIBaseDemoApp::initialize.

    /** Initialize local address (@ref m_local_address) **/
    // Done in @ref RmxAPIBaseDemoApp::initialize_local_address, called from @ref RmxAPIBaseDemoApp::initialize.

    /* Initialize Rivermax library */
    rmx_status status = rmx_init();
    EXIT_ON_FAILURE(status, "Failed to initialize Rivermax library");

    /* Allocate memory */
    auto mem_allocator = m_rmax_apps_lib.get_memory_allocator(AllocatorType::Malloc, m_app_settings);
    auto mem_utils = mem_allocator->get_memory_utils();
    auto mem_alignment = get_cache_line_size();

    const size_t data_stride_size = align_up_pow2(m_app_settings->packet_payload_size, get_cache_line_size());
    size_t block_memory_size = m_app_settings->num_of_chunks_in_mem_block *
        m_app_settings->num_of_packets_in_chunk * data_stride_size;
    size_t total_size_in_bytes = m_app_settings->num_of_memory_blocks * block_memory_size;

    void* allocated_mem_ptr = mem_allocator->allocate_aligned(total_size_in_bytes, mem_alignment);
    EXIT_ON_CONDITION_WITH_CLEANUP(allocated_mem_ptr == nullptr, "Failed to allocate memory");
    auto rc = mem_utils->memory_set(allocated_mem_ptr, 0, total_size_in_bytes);
    EXIT_ON_CONDITION_WITH_CLEANUP(rc != ReturnStatus::success, "Failed to set memory");

    /* Set memory layout */
    std::vector<rmx_output_media_mem_block> mem_blocks(m_app_settings->num_of_memory_blocks);
    rmx_output_media_init_mem_blocks(mem_blocks.data(), mem_blocks.size());
    constexpr size_t num_of_sub_blocks = 1; // Non-HDS mode
    constexpr size_t sub_block_index = 0;

    std::vector<uint16_t> payload_sizes(
        m_app_settings->num_of_packets_in_mem_block, m_app_settings->packet_payload_size);
    for (auto& block : mem_blocks) {
        rmx_output_media_set_sub_block_count(&block, num_of_sub_blocks);
        rmx_output_media_set_chunk_count(&block, m_app_settings->num_of_chunks_in_mem_block);
        rmx_output_media_set_packet_layout(&block, sub_block_index, payload_sizes.data());
    }

    /* Set memory region */
    rmx_mem_region* mem_region;
    constexpr size_t sub_block_id = 0;
    char* curr_block_addr = static_cast<char*>(allocated_mem_ptr);
    for (auto& block : mem_blocks) {
        mem_region = rmx_output_media_get_sub_block(&block, sub_block_id);
        EXIT_ON_CONDITION(mem_region == nullptr, "Failed to get memory block");
        mem_region->addr = curr_block_addr;
        mem_region->length = block_memory_size;
        mem_region->mkey = RMX_MKEY_INVALID;
        curr_block_addr += block_memory_size;
    }

    /* Create media stream */

    /** Set stream parameters **/
    rmx_output_media_stream_params stream_params;

    rmx_output_media_init(&stream_params);
    rmx_output_media_assign_mem_blocks(&stream_params, mem_blocks.data(), mem_blocks.size());
    rmx_output_media_set_sdp(&stream_params, m_app_settings->media.sdp.c_str());
    rmx_output_media_set_idx_in_sdp(&stream_params, m_app_settings->media.media_block_index);
    rmx_output_media_set_packets_per_chunk(&stream_params, m_app_settings->num_of_packets_in_chunk);
    rmx_output_media_set_stride_size(&stream_params, sub_block_id, data_stride_size);
    rmx_output_media_set_packets_per_frame(&stream_params, m_app_settings->media.packets_in_frame_field);

    /** Create the stream **/
    rmx_stream_id stream_id;
    status = rmx_output_media_create_stream(&stream_params, &stream_id);
    EXIT_ON_FAILURE_WITH_CLEANUP(status, "Failed to create stream");

    /**
     * Set commit start time
     *
     * @note The send time should be calculated based on SMPTE 2110 standards.
     * For this example, we use a fixed value.
     */
    uint64_t send_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        (std::chrono::system_clock::now() + std::chrono::seconds{ 1 }).time_since_epoch()).count();

    const uint64_t start_send_time_ns = send_time_ns;

    uint64_t sent_mem_block_counter = 0;
    auto get_send_time_ns = [&]() { return static_cast<uint64_t>(
        start_send_time_ns
        + m_app_settings->media.frame_field_time_interval_ns
        * m_app_settings->media.frames_fields_in_mem_block
        * sent_mem_block_counter);
    };
    uint64_t commit_timestamp_ns = 0;
    uint64_t chunk_in_frame_counter = 0;

    /* Initialize chunk handle */
    rmx_output_media_chunk_handle chunk_handle;

    rmx_output_media_init_chunk_handle(&chunk_handle, stream_id);

    /* Data path loop */

    uint16_t* payload_sizes_ptr = nullptr;
    uint8_t* payload_ptr = nullptr;
    auto first_chunk_in_frame = false;

    while (likely(status == RMX_OK && SignalHandler::get_received_signal() < 0)) {
        chunk_in_frame_counter = 0;
        send_time_ns = get_send_time_ns();

        /** Prepare and send a frame **/
        do {
            /*** Get the next chunk to send ***/
            do {
                status = rmx_output_media_get_next_chunk(&chunk_handle);
            } while (unlikely(status == RMX_NO_FREE_CHUNK));
            EXIT_ON_FAILURE_WITH_CLEANUP(status, "Failed to get next chunk");

            /*** Prepare chunk's data ***/
            payload_sizes_ptr = rmx_output_media_get_chunk_packet_sizes(&chunk_handle, sub_block_index);
            payload_ptr = reinterpret_cast<uint8_t*>(rmx_output_media_get_chunk_strides(&chunk_handle, sub_block_index));

            /**
             * @note Fill the chunk with RTP headers and media payload based on SMPTE 2110 standards.
             * For this example, we skip this step.
             */
            NOT_IN_USE(payload_sizes_ptr);
            NOT_IN_USE(payload_ptr);

            /*** Commit the chunk ***/
            first_chunk_in_frame = unlikely(chunk_in_frame_counter % m_app_settings->media.chunks_in_frame_field == 0);
            commit_timestamp_ns = first_chunk_in_frame ? send_time_ns : 0;
            do {
                status = rmx_output_media_commit_chunk(&chunk_handle, commit_timestamp_ns);
            } while (unlikely(status == RMX_HW_SEND_QUEUE_IS_FULL));
            EXIT_ON_FAILURE_WITH_CLEANUP(status, "Failed to commit chunk");

        } while (likely(status == RMX_OK && ++chunk_in_frame_counter < m_app_settings->media.chunks_in_frame_field));
        sent_mem_block_counter++;
    }

    /* Destroy media stream */
    do {
        status = rmx_output_media_destroy_stream(stream_id);
    } while (status == RMX_BUSY);
    EXIT_ON_FAILURE_WITH_CLEANUP(status, "Failed to destroy stream");

    /* Clean-up Rivermax library */
    status = rmx_cleanup();
    EXIT_ON_FAILURE(status, "Failed to clean-up Rivermax library");

    return ReturnStatus::success;
}

int main(int argc, const char *argv[])
{
    return RmxMemoryAllocationMediaSendDemoApp().run(argc, argv);
}
