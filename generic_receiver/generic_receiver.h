/*
 * SPDX-FileCopyrightText: Copyright (c) 2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include "gpu.h"
#include "checksum_header.h"
#include "memory_allocator.h"

using std::chrono::high_resolution_clock;
using std::chrono::duration_cast;
using std::chrono::seconds;
using std::chrono::milliseconds;
using std::chrono::microseconds;
using std::chrono::nanoseconds;

struct Statistics {
    uint64_t received_packets = 0;
    uint64_t received_bytes = 0;
    uint32_t last_sequence = 0;
    uint32_t dropped_packets = 0;
    uint32_t checksum_mismatch = 0;

    /* GPU-allocated counter for checksum mismatches. */
    uint32_t *gpu_checksum_mismatch = nullptr;

    void reset()
    {
        received_packets = 0;
        received_bytes = 0;
        dropped_packets = 0;
        checksum_mismatch = 0;

        if (gpu_checksum_mismatch) {
            gpu_reset_counter(gpu_checksum_mismatch);
        }
    }

    double received_mbits()
    {
        return ((received_bytes * 8) / 1.e6);
    }
};

class RxStream {
public:

    /**
     * @param buffer_elements The number of elements (packets) to allocate in the stream.
     * @param payload_size The size of each payload, in bytes.
     * @param header_size The size of each header, in bytes. If zero, this implies that any headers
     *                    included with packets will be written at the start of each payload.
     * @param addr The network address on which this stream will be receiving data.
     * @param gpu The GPU to use for GPUDirect (-1 == don't use GPU).
     */
    RxStream(rmx_input_stream_params_type rx_type
            , rmx_input_timestamp_format timestamp_format
            , uint32_t buffer_elements
            , uint16_t payload_size
            , uint16_t header_size
            , const sockaddr_in &addr
            , int gpu
            , bool wait_for_event
            , bool use_checksum_header
            , const std::vector<int>& cpu_affinity);

    virtual ~RxStream();

    /**
     * Returns whether or not the stream is initialized.
     */
    bool is_initialized() const
    {
        return (m_stream_id != INVALID_STREAM_ID);
    }

    /**
     * Returns true if header data split is used
     */
    bool is_hds_used() const
    {
        return m_header_size != 0;
    }

    /**
     * Allocate memory for the payload and header buffers.
     */
    virtual bool allocate_memory();

    /**
     * Create the Rivermax stream.
     */

    bool create_stream();

    /**
     * Destroy the Rivermax stream.
     */
    void destroy_stream()
    {
        if (is_initialized()) {
            const rmx_status status = rmx_input_destroy_stream(m_stream_id);
            if (status != RMX_OK) {
                std::cerr << "Failed to destroy stream. Error: " << status << std::endl;
            }
            m_stream_id = INVALID_STREAM_ID;
        }
    }

    /**
     * Attach a flow to the stream.
     */
    bool attach_flow(const sockaddr_in& destination_address, const sockaddr_in& remote_address, uint32_t flow_tag);

    /**
     * Detach the flow from the stream.
     */
    virtual void detach_flow();

    /**
     * Initialize wait mode.
     */
    bool init_wait();

    void* allocate_buffer(std::unique_ptr<MemoryAllocator> &mem_allocator, std::shared_ptr<MemoryUtils> &mem_utils,
        size_t buffer_len, size_t align, bool allow_fallback);

    /**
     * Gets the next sequence of packets from the stream.
     */
    virtual rmx_status get_next_chunk(const rmx_input_completion *&comp);

    /**
     * Configures desired stream parameters
     */
    virtual void configure_stream();

    /**
     * Initializes and creates stream with desired parameters
     */
    virtual bool stream_initialize();

    /**
     * Main receive loop; continues until an error occurs or the stream is signaled (e.g. CTRL-C).
     */
    bool main_loop();

    /**
     * @brief: check packets drop.
     *
     * @param [in] sequence: Sequence number of processed packet.
     */
    void check_packets_drop(uint32_t sequence);

    /**
     * Process the packets in a received chunk.
     */
    void process_packets(const rmx_input_completion *comp);

    /**
     * Calculate and compare the checksum for the data.
     */
    void host_compare_checksum(uint32_t expected, const uint8_t *data, size_t size);

    /**
     * Updates the receive statistics.
     */
    virtual void update_statistics(high_resolution_clock::time_point& start_time);

protected:

    // Used to detect if we have a valid stream
    static const rmx_stream_id INVALID_STREAM_ID = static_cast<rmx_stream_id>(-1L);

    // Input stream parameters used for stream creation.
    rmx_input_stream_params m_stream_params;

    // ID for the Rivermax stream object.
    rmx_stream_id m_stream_id;

    // Event manager used in "wait" mode.
    EventMgr m_event_mgr;

    // Network flow descriptor used for attachment.
    rmx_input_flow m_receive_flow;

    // Memory region index for data
    const size_t m_payload_mem_block_id;

    // Memory region index for header if used
    const size_t m_header_mem_block_id;

    // Number of packets allocated
    size_t m_buffer_elements;

    // Expected payload size to be received
    uint16_t m_payload_size;

    // Stride for a single payload
    size_t m_data_stride_size;

    // Expected header size to be received
    uint16_t m_header_size;

    // Stride for a single header
    size_t m_header_stride_size;

    // Whether flow has been attached or not.
    bool m_is_flow_attached;

    // Memory region used for payloads
    rmx_mem_region* m_data_memory = nullptr;

    // Statistics about input stream
    Statistics m_statistics;

    // Rivermax input stream type
    rmx_input_stream_params_type m_rx_type;

    // Network interface on which this stream will be receiving data
    sockaddr_in m_addr;

    // GPU to use for GPUDirect allocations (GPU disabled when < 0).
    int m_gpu;

    // Allocator used for header memory
    std::unique_ptr<MemoryAllocator> m_mem_hdr_allocator;

    // Allocator used for payload memory
    std::unique_ptr<MemoryAllocator> m_mem_payload_allocator;

    // Utilities to interact with header memory
    std::shared_ptr<MemoryUtils> m_mem_hdr_utils;

    // Utilities to interact with payload memory
    std::shared_ptr<MemoryUtils> m_mem_payload_utils;

private:

    // Memory region used for headers
    rmx_mem_region* m_header_memory = nullptr;

    // Rivermax flow tag that is attached to the stream.
    uint32_t m_flow_tag;

    // Whether or not to use wait mode.
    bool m_wait_for_event;

    // Whether or not to use the ChecksumHeader.
    bool m_use_checksum_header;

    // Indicates if first packet has been received.
    bool m_first_pkt;

    // Cpu cores to do static affinity on.
    const std::vector<int>& m_cpu_affinity;

    // Indicates gpu memory allocation size, returned by CUDA allocation function.
    // May differ from requested size because of CUDA alignment restrictions to 2MIB.
    size_t m_cuda_buffer_size;

    // Chunk handle used to acquire chunks for the associated stream
    rmx_input_chunk_handle m_chunk_handle;

    // Desired timestamp format for incoming packets
    rmx_input_timestamp_format m_timestamp_format;

    /**
     * Initialize stream specific event channel.
     */
    bool init_event_channel();
};
