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
    RxStream(rmax_in_stream_type rx_type, uint64_t in_buffer_attr_flags,
            rmax_in_timestamp_format timestamp_format,
            uint32_t buffer_elements, uint16_t payload_size,
             uint16_t header_size, const sockaddr_in &addr,
             int gpu, bool use_checksum_header, const std::vector<int>& cpu_affinity)
        : m_stream_id(INVALID_STREAM_ID), m_rx_type(rx_type), m_addr(addr), m_gpu(gpu)
        , m_use_checksum_header(use_checksum_header), m_first_pkt(true),
        m_cpu_affinity(cpu_affinity), m_timestamp_format(timestamp_format)
    {
        std::cout << "Creating Receive Stream." << std::endl
                  << "    Packets: " << buffer_elements << std::endl
                  << "    Payload size: " << payload_size << std::endl
                  << "    Header size: " << header_size << std::endl;

        // The stream initially has no flows attached.
        memset(&m_flow, 0, sizeof(m_flow));
        m_flow.flow_id = INVALID_FLOW_ID;

        // Configure the buffer attributes.
        memset(&m_buffer, 0, sizeof(m_buffer));
        m_buffer.attr_flags = in_buffer_attr_flags;
        m_buffer.num_of_elements = buffer_elements;

        // Configure the payload buffer.
        memset(&m_data, 0, sizeof(m_data));
        m_data.min_size = payload_size;
        m_data.max_size = payload_size;
        m_buffer.data = &m_data;

        // Configure the header buffer.
        memset(&m_header, 0, sizeof(m_header));
        if (use_checksum_header) {
            header_size = sizeof(ChecksumHeader);
        }
        m_header.min_size = header_size;
        m_header.max_size = header_size;
        m_buffer.hdr = header_size ? &m_header : NULL;

        if (m_gpu != GPU_ID_INVALID) {
            m_mem_payload_allocator.reset(new GpuMemoryAllocator(m_gpu));
        } else {
            m_mem_payload_allocator.reset(new HugePagesMemoryAllocator());
        }
        m_mem_payload_utils = m_mem_payload_allocator->get_memory_utils();
        m_mem_hdr_allocator.reset(new HugePagesMemoryAllocator());
        m_mem_hdr_utils = m_mem_hdr_allocator->get_memory_utils();
    }

    virtual ~RxStream()
    {
        detach_flow();
        destroy_stream();
    }

    /**
     * Returns whether or not the stream is initialized.
     */
    bool is_initialized()
    {
        return (m_stream_id != INVALID_STREAM_ID);
    }

    /**
     * Allocate memory for the payload and header buffers.
     */
    virtual bool allocate_memory()
    {
        // Determine the allocation size for the payload and header buffers.
        size_t payload_len;
        size_t header_len;
        rmax_status_t status = rmax_in_query_buffer_size(m_rx_type, &m_addr, &m_buffer,
                                                         &payload_len, &header_len);
        if (status != RMAX_OK) {
            std::cerr << "Failed calling rmax_in_query_buffer_size. Error: " << status << std::endl;
            return false;
        }

        // Fail if payload/header splitting was requested but it's not supported by Rivermax
        // (i.e. returned header length from Rivermax is 0).
        if (m_header.min_size != 0 && header_len == 0) {
            std::cerr << "Failed to allocate stream; payload/header splitting not supported." << std::endl;
            return false;
        }

        std::cout << "Allocating memory for " << m_buffer.num_of_elements << " packets." << std::endl
                  << "    Payload allocation size: " << payload_len << std::endl
                  << "    Header allocation size: " << header_len << std::endl;

        // Determine the allocation alignment.
        size_t alignment = (size_t)get_cache_line_size();

        // In all cases when memory allocated on Host memory, doing fallback to default memory allocation if failed
        // Note: header always allocated on Host memory for now
        bool allow_fallback = m_gpu != GPU_ID_INVALID ? false : true;

        // Allocate the payload buffer.
        m_data.ptr = allocate_buffer(m_mem_payload_allocator, m_mem_payload_utils, payload_len, alignment, allow_fallback);
        if (m_gpu != GPU_ID_INVALID) {
            m_statistics.gpu_checksum_mismatch = gpu_allocate_counter();
            if (!m_statistics.gpu_checksum_mismatch) {
                std::cerr << "Failed to allocate GPU counter." << std::endl;
                return false;
            }
        }
        if (!m_data.ptr) {
            std::cerr << "Failed to allocate payload memory." << std::endl;
            return false;
        }

        // Allocate the header buffer (if required).
        if (header_len) {
            m_header.ptr = allocate_buffer(m_mem_hdr_allocator, m_mem_hdr_utils, header_len, alignment, true);
            if (!m_header.ptr) {
                std::cerr << "Failed to allocate header memory." << std::endl;
                return false;
            }
        }

        return true;
    }

    /**
     * Create the Rivermax stream.
     */
    bool create_stream()
    {
        if (m_stream_id != INVALID_STREAM_ID) {
            std::cerr << "Stream already exists." << std::endl;
            return false;
        }

        rmax_status_t status = rmax_in_create_stream(m_rx_type, &m_addr, &m_buffer,
                                                     m_timestamp_format,
                                                     RMAX_IN_CREATE_STREAM_INFO_PER_PACKET,
                                                     &m_stream_id);
        if (status != RMAX_OK) {
            if (m_gpu != GPU_ID_INVALID) {
                size_t total_allocated_size = m_buffer.num_of_elements * m_buffer.data->stride_size;
                gpu_verify_allocated_bar1_size(m_gpu, total_allocated_size);
            }
            std::cerr << "Failed to create Rivermax stream. Error: " << status << std::endl;
            return false;
        }

        std::cout << "Created stream for " << m_buffer.num_of_elements << " packets." << std::endl
                  << "    Payload stride: " << m_buffer.data->stride_size << std::endl
                  << "    Header stride: " << (m_buffer.hdr ? m_buffer.hdr->stride_size : 0) << std::endl;

        return true;
    }

    /**
     * Destroy the Rivermax stream.
     */
    void destroy_stream()
    {
        if (m_stream_id != INVALID_STREAM_ID) {
            rmax_status_t status = rmax_in_destroy_stream(m_stream_id);
            if (status != RMAX_OK) {
                std::cerr << "Failed to destroy stream. Error: " << status << std::endl;
            }
            m_stream_id = INVALID_STREAM_ID;
        }
    }

    /**
     * Attach a flow to the stream.
     */
    bool attach_flow(rmax_in_flow_attr flow)
    {
        if (m_stream_id == INVALID_STREAM_ID) {
            std::cerr << "Failed to attach flow; stream not initialized." << std::endl;
            return false;
        }

        if (m_flow.flow_id != INVALID_FLOW_ID) {
            std::cerr << "Failed to attach flow; flow already attached." << std::endl;
            return false;
        }

        rmax_status_t status = rmax_in_attach_flow(m_stream_id, &flow);
        if (status != RMAX_OK) {
            std::cerr << "Failed to attach flow. Error: " << status << std::endl;
            return false;
        }

        std::cout << "Attached flow " << flow.flow_id << " to stream." << std::endl;

        m_flow = flow;

        return true;
    }

    /**
     * Detach the flow from the stream.
     */
    virtual void detach_flow()
    {
        if (m_flow.flow_id != INVALID_FLOW_ID) {
            rmax_status_t status = rmax_in_detach_flow(m_stream_id, &m_flow);
            if ((status != RMAX_OK) && (status != RMAX_SIGNAL)) {
                std::cerr << "Failed to detach flow. Error: " << status << std::endl;
            }

            memset(&m_flow, 0, sizeof(m_flow));
            m_flow.flow_id = INVALID_FLOW_ID;
        }
    }

    void* allocate_buffer(std::unique_ptr<MemoryAllocator> &mem_allocator, std::shared_ptr<MemoryUtils> &mem_utils,
        size_t buffer_len, size_t align, bool allow_fallback)
    {
        void* ptr_mem = mem_allocator->allocate(buffer_len);
        if (ptr_mem == nullptr && allow_fallback) {
            std::cout << "Fallback to malloc memory allocation" << std::endl;
            mem_allocator.reset(new MallocMemoryAllocator());
            mem_utils = mem_allocator->get_memory_utils();
            ptr_mem = mem_allocator->allocate(buffer_len, align);
        }
        if (ptr_mem == nullptr) {
            std::cerr << "Failed to allocate memory with size: " << buffer_len << std::endl;
            return nullptr;
        }
        mem_utils->memory_set(ptr_mem, 0, buffer_len);

        set_allocation_size(buffer_len);

        std::cout << "Memory allocated." << std::endl
                  << "    Size: " << buffer_len << std::endl
                  << "    Ptr: " << std::hex << ptr_mem << std::dec << std::endl;

        return ptr_mem;
    }

    virtual void set_allocation_size(const size_t &size)
    {
        NOT_IN_USE(size);
    };

    /**
     * Gets the next sequence of packets from the stream.
     */
    virtual rmax_status_t get_next_chunk(rmax_in_completion &comp)
    {
        const int min_packets = 0;
        const int max_packets = 5000;
        const int timeout = 0; // Wait forever.
        const int flags = 0;

        if (m_stream_id == INVALID_STREAM_ID) {
            std::cerr << "Error: Stream not initialized." << std::endl;
            return RMAX_ERR_NOT_INITIALAZED;
        }

        rmax_status_t status = rmax_in_get_next_chunk(m_stream_id, min_packets, max_packets, timeout, flags, &comp);
        if (status != RMAX_OK && status != RMAX_SIGNAL) {
            std::cerr << "Failed to get next chunk. Error: " << status << std::endl;
        }
        return status;
    }

    virtual bool stream_initialize()
    {
        bool status;

        // Allocate memory for the stream.
        status = allocate_memory();
        if (!status) {
            std::cerr << "Failed to allocate memory for the stream." << std::endl;
            return status;
        }

        // Create the Rivermax stream.
        status = create_stream();
        if (!status || !is_initialized()) {
            std::cerr << "Failed to create stream. " << status << std::endl;
            return false;
        }

        return true;
    }

    /**
     * Main receive loop; continues until an error occurs or the stream is signaled (e.g. CTRL-C).
     */
    bool main_loop()
    {
        // Check if user set cpu affinity
        if (m_cpu_affinity.size() > 0) {
            set_cpu_affinity(m_cpu_affinity);
        }

        auto start_time = high_resolution_clock::now();
        rmax_in_completion comp;
        while (!exit_app()) {
            // Get the next chunk of packets from the stream.
            memset(&comp, 0, sizeof(comp));
            rmax_status_t rmax_status = get_next_chunk(comp);
            if (rmax_status == RMAX_SIGNAL) {
                return true;
            }
            if (rmax_status != RMAX_OK) {
                return false;
            }

            // Process the packets.
            process_packets(comp);

            // TODO Wait for event or optimize polling interval.
            //std::this_thread::sleep_for(microseconds(500));

            // Update the receive statistics.
            update_statistics(start_time);
        }

        return true;
    }

    /**
     * @brief: check packets drop.
     *
     * @param [in] sequence: Sequence number of processed packet.
     */
    void check_packets_drop(uint32_t sequence) {
        uint32_t curr_drops = 0;

        if (!m_first_pkt) {
            if (sequence < m_statistics.last_sequence) {
                // In case drops are during wrap around.
                // The extra parentheses are to avoid collision with max() macro defined in windows headers.
                curr_drops = sequence + ((std::numeric_limits<uint32_t>::max)() - m_statistics.last_sequence);
            } else {
                curr_drops = sequence - (m_statistics.last_sequence + 1);
            }
            m_statistics.dropped_packets += curr_drops;
        }

        m_statistics.last_sequence = sequence;
        m_first_pkt = false;
    }

    /**
     * Process the packets in a received chunk.
     */
    void process_packets(const rmax_in_completion &comp)
    {
        for (uint64_t packet_index = 0; packet_index < comp.chunk_size; ++packet_index) {
            uint8_t *data = (uint8_t*)comp.data_ptr + packet_index * (size_t)m_data.stride_size;
            uint8_t *header;
            size_t data_size = comp.packet_info_arr[packet_index].data_size;
            size_t header_size = comp.packet_info_arr[packet_index].hdr_size;

            // If there's a separate header buffer, then the header will be written to that buffer.
            // Otherwise, the header will precede the data in the payload buffer.
            if (comp.hdr_ptr) {
                header = (uint8_t*)comp.hdr_ptr + packet_index * (size_t)m_header.stride_size;
            } else {
                header = data;
                data += header_size;
            }

            // Update the statistics.
            m_statistics.received_packets++;
            m_statistics.received_bytes += data_size;
            m_statistics.received_bytes += header_size;

            if (m_use_checksum_header) {
                ChecksumHeader *hdr = (ChecksumHeader*)header;

                check_packets_drop(ntohl(hdr->sequence));

                // Calculate and compare the packet checksum.
                uint32_t checksum = ntohl(hdr->checksum);
                if (m_gpu == GPU_ID_INVALID) {
                    host_compare_checksum(checksum, data, data_size);
                } else {
                    gpu_compare_checksum(checksum, data, data_size, m_statistics.gpu_checksum_mismatch);
                }
            }
        }
    }

    /**
     * Calculate and compare the checksum for the data.
     */
    void host_compare_checksum(uint32_t expected, unsigned char *data, size_t size)
    {
        uint32_t checksum = 0;
        for (size_t i = 0; i < size; i++) {
            checksum += (unsigned char)data[i];
        }
        if (checksum != expected) {
            m_statistics.checksum_mismatch++;
        }
    }

    /**
     * Updates the receive statistics.
     */
    virtual void update_statistics(high_resolution_clock::time_point& start_time)
    {
        auto now = high_resolution_clock::now();
        auto diff = duration_cast<microseconds>(now - start_time);
        if (diff >= seconds{1}) {
            start_time = now;
            std::cout << "Got " << std::setw(7) << m_statistics.received_packets
                      << ((m_gpu == GPU_ID_INVALID) ? "" : " GPU") << " packets | "
                      << std::fixed << std::setprecision(2);

            double mbits_received = m_statistics.received_mbits();
            if (mbits_received > 1000.) {
                std::cout << std::setw(4) << (mbits_received / 1000.) << " Gbps during ";
            } else {
                std::cout << std::setw(4) << mbits_received << " Mbps during ";
            }
            std::cout << std::setw(4) << ((double)diff.count() / 1.e6) << " sec";

            if (m_use_checksum_header) {
                if (m_gpu != GPU_ID_INVALID) {
                    m_statistics.checksum_mismatch = gpu_read_counter(m_statistics.gpu_checksum_mismatch);
                }
                std::cout << " | " << m_statistics.dropped_packets << " dropped packets"
                          << " | " << m_statistics.checksum_mismatch << " checksum errors";
            }

            std::cout << std::endl;

            m_statistics.reset();
        }
    }

protected:
    static const int INVALID_STREAM_ID = -1;
    static const uint32_t INVALID_FLOW_ID = 0;
    rmax_stream_id m_stream_id;       //< ID for the Rivermax stream object.
    struct rmax_in_memblock m_data;   //< Memory block used for the data (payloads).
    Statistics m_statistics;          //< Receive statistics for the stream.
    rmax_in_stream_type m_rx_type;    // Rivermax input stream types
    sockaddr_in m_addr;               //< Network address on which this stream will be receiving data.
    rmax_in_buffer_attr m_buffer;     //< The buffer attributes for the stream.
    int m_gpu;                        //< GPU to use for GPUDirect allocations (GPU disabled when < 0).
    std::unique_ptr<MemoryAllocator> m_mem_hdr_allocator;
    std::unique_ptr<MemoryAllocator> m_mem_payload_allocator;
    std::shared_ptr<MemoryUtils> m_mem_hdr_utils;
    std::shared_ptr<MemoryUtils> m_mem_payload_utils;

private:
    struct rmax_in_memblock m_header; //< Memory block used for the headers.
    rmax_in_flow_attr m_flow;         //< Rivermax flow that is attached to the stream.
    bool m_use_checksum_header;       //< Whether or not to use the ChecksumHeader.
    bool m_first_pkt;                 //< Indicate first packet received.
    const std::vector<int>& m_cpu_affinity; //< Cpu cores to do static affinity on.
    size_t m_cuda_buffer_size;        //< Indicates gpu memory allocation size, returned by CUDA allocation function. May differ from requested size because of CUDA alignment restrictions to 2MIB.
    rmax_in_timestamp_format m_timestamp_format;
};
