/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include <stdlib.h>
#include <string.h>
#include <iostream>
#include <chrono>
#include <thread>
#include <rivermax_api.h>
#include "CLI/CLI.hpp"
#include "rt_threads.h"
#ifdef __linux__
#include <arpa/inet.h>
#else
#include <Ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#endif
#include "generic_receiver.h"

RxStream::RxStream(rmx_input_stream_params_type rx_type
                 , rmx_input_timestamp_format timestamp_format
                 , uint32_t buffer_elements
                 , uint16_t payload_size
                 , uint16_t header_size
                 , const sockaddr_in &addr
                 , int gpu
                 , bool wait_for_event
                 , bool use_checksum_header
                 , const std::vector<int>& cpu_affinity)
        : m_stream_id(INVALID_STREAM_ID)
        , m_payload_mem_block_id(header_size ? 1 : 0)
        , m_header_mem_block_id(header_size ? 0 : 1)
        , m_buffer_elements(buffer_elements)
        , m_payload_size(payload_size)
        , m_data_stride_size(0)
        , m_header_size(header_size)
        , m_header_stride_size(0)
        , m_is_flow_attached(false)
        , m_rx_type(rx_type)
        , m_addr(addr)
        , m_gpu(gpu)
        , m_flow_tag(0)
        , m_wait_for_event(wait_for_event)
        , m_use_checksum_header(use_checksum_header)
        , m_first_pkt(true)
        , m_cpu_affinity(cpu_affinity)
        , m_timestamp_format(timestamp_format)
{
        std::cout << "Creating Receive Stream." << std::endl
                  << "    Packets: " << buffer_elements << std::endl
                  << "    Payload size: " << payload_size << std::endl
                  << "    Header size: " << header_size << std::endl;

        if (m_gpu != GPU_ID_INVALID) {
            m_mem_payload_allocator.reset(new GpuMemoryAllocator(m_gpu));
        } else {
            m_mem_payload_allocator.reset(new HugePagesMemoryAllocator());
        }
        m_mem_payload_utils = m_mem_payload_allocator->get_memory_utils();
        m_mem_hdr_allocator.reset(new HugePagesMemoryAllocator());
        m_mem_hdr_utils = m_mem_hdr_allocator->get_memory_utils();
}

RxStream::~RxStream()
{
    detach_flow();
    destroy_stream();
}

bool RxStream::allocate_memory()
{
    const rmx_status status = rmx_input_determine_mem_layout(&m_stream_params);
    if (status != RMX_OK) {
        std::cerr << "Failed calling rmx_input_determine_mem_layout. Error: " << status << std::endl;
        return status;
    }

    m_buffer_elements = rmx_input_get_mem_capacity_in_packets(&m_stream_params);
    m_data_memory = rmx_input_get_mem_block_buffer(&m_stream_params, m_payload_mem_block_id);
    m_data_stride_size = rmx_input_get_stride_size(&m_stream_params, m_payload_mem_block_id);

    if (is_hds_used()) {
        m_header_memory = rmx_input_get_mem_block_buffer(&m_stream_params, m_header_mem_block_id);
        m_header_stride_size = rmx_input_get_stride_size(&m_stream_params, m_header_mem_block_id);
    }

    // Fail if payload/header splitting was requested but it's not supported by Rivermax
    if (is_hds_used() && (m_header_memory == nullptr || m_header_memory->length == 0)) {
        std::cerr << "Failed to allocate stream; payload/header splitting not supported." << std::endl;
        return false;
    }

    const size_t payload_length = m_data_memory->length;
    const size_t header_length = m_header_memory ? m_header_memory->length : 0;
    std::cout << "Allocating memory for " << m_buffer_elements << " packets." << std::endl
                << "    Payload allocation size: " << payload_length << std::endl
                << "    Header allocation size: " << header_length << std::endl;

    // Determine the allocation alignment.
    const size_t alignment = get_cache_line_size();

    // In all cases when memory allocated on Host memory, doing fallback to default memory allocation if failed
    // Note: header always allocated on Host memory for now
    const bool allow_fallback = m_gpu != GPU_ID_INVALID ? false : true;

    // Allocate the payload buffer.
    void* payload_ptr = allocate_buffer(m_mem_payload_allocator, m_mem_payload_utils, payload_length, alignment, allow_fallback);
    if (m_gpu != GPU_ID_INVALID) {
        m_statistics.gpu_checksum_mismatch = gpu_allocate_counter();
        if (!m_statistics.gpu_checksum_mismatch) {
            std::cerr << "Failed to allocate GPU counter." << std::endl;
            return false;
        }
    }
    if (!payload_ptr) {
        std::cerr << "Failed to allocate payload memory." << std::endl;
        return false;
    }

    // Setup data memory region with allocated buffer
    m_data_memory->addr = payload_ptr;
    m_data_memory->mkey = RMX_MKEY_INVALID;

    // Allocate the header buffer (if required).
    if (header_length) {
        constexpr bool can_allocator_fallback = true;
        void* header_ptr = allocate_buffer(m_mem_hdr_allocator, m_mem_hdr_utils, header_length, alignment, can_allocator_fallback);
        if (!header_ptr) {
            std::cerr << "Failed to allocate header memory." << std::endl;
            return false;
        }

        // Setup header memory region with allocated buffer
        m_header_memory->addr = header_ptr;
        m_header_memory->mkey = RMX_MKEY_INVALID;
    }

    return true;
}

bool RxStream::create_stream()
{
    if (is_initialized()) {
        std::cerr << "Stream already exists." << std::endl;
        return false;
    }

    rmx_status status = rmx_input_create_stream(&m_stream_params, &m_stream_id);
    if (status != RMX_OK) {
        if (m_gpu != GPU_ID_INVALID) {
            const size_t total_allocated_size = m_buffer_elements * m_data_stride_size;
            gpu_verify_allocated_bar1_size(m_gpu, total_allocated_size);
        }
        std::cerr << "Failed to create Rivermax stream. Error: " << status << std::endl;
        return false;
    }

    // Completion moderation config. Not configurable at the moment
    constexpr size_t min_chunk_size = 0;
    constexpr size_t max_chunk_size = 5000;
    constexpr int timeout_next_chunk = 0;
    status = rmx_input_set_completion_moderation(m_stream_id, min_chunk_size, max_chunk_size, timeout_next_chunk);

    if (status != RMX_OK) {
        std::cerr << "Failed to set expected packets count for stream: " << m_stream_id << ", with status: "
            << status << std::endl;
    }

    // Init chunk handle used to retrieve chunks for our stream
    rmx_input_init_chunk_handle(&m_chunk_handle, m_stream_id);

    std::cout << "Created stream for " << m_buffer_elements << " packets." << std::endl
                << "    Payload stride: " << m_data_stride_size << std::endl
                << "    Header stride: " << m_header_stride_size << std::endl;

    return true;
}

bool RxStream::attach_flow(const sockaddr_in& destination_address, const sockaddr_in& remote_address, uint32_t flow_tag)
{
    if (!is_initialized()) {
        std::cerr << "Failed to attach flow; stream not initialized." << std::endl;
        return false;
    }

    if (m_is_flow_attached) {
        std::cerr << "Failed to attach flow; flow already attached." << std::endl;
        return false;
    }

    rmx_input_flow receive_flow;
    rmx_input_init_flow(&receive_flow);
    rmx_input_set_flow_local_addr(&receive_flow, reinterpret_cast<const sockaddr*>(&destination_address));
    rmx_input_set_flow_remote_addr(&receive_flow, reinterpret_cast<const sockaddr*>(&remote_address));
    rmx_input_set_flow_tag(&receive_flow, flow_tag);

    const rmx_status status = rmx_input_attach_flow(m_stream_id, &receive_flow);
    if (status != RMX_OK) {
        std::cerr << "Failed to attach flow. Error: " << status << std::endl;
        return false;
    }

    std::cout << "Attached flow " << flow_tag << " to stream." << std::endl;

    m_flow_tag = flow_tag;
    m_receive_flow = receive_flow;
    m_is_flow_attached = true;

    return true;
}

void RxStream::detach_flow()
{
    if (m_is_flow_attached) {
        const rmx_status status = rmx_input_detach_flow(m_stream_id, &m_receive_flow);
        if ((status != RMX_OK) && (status != RMX_SIGNAL)) {
            std::cerr << "Failed to detach flow. Error: " << status << std::endl;
        }

        m_is_flow_attached = false;
    }
}

bool RxStream::init_event_channel()
{
    return m_event_mgr.init(m_stream_id);
}

bool RxStream::init_wait()
{
    if (m_wait_for_event) {
        if (!init_event_channel()) {
            return false;
        }
    }
    return true;
}

void* RxStream::allocate_buffer(std::unique_ptr<MemoryAllocator> &mem_allocator, std::shared_ptr<MemoryUtils> &mem_utils,
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

    std::cout << "Memory allocated." << std::endl
                << "    Size: " << buffer_len << std::endl
                << "    Ptr: " << std::hex << ptr_mem << std::dec << std::endl;

    return ptr_mem;
}

rmx_status RxStream::get_next_chunk(const rmx_input_completion *&comp)
{
    rmx_status status;
    if (!is_initialized()) {
        std::cerr << "Error: Stream not initialized." << std::endl;
        return RMX_NOT_INITIALIZED;
    }

    if (m_wait_for_event) {
        // Do a single poll before waiting for events. Otherwise, there is
        // a risk to block forever if there are no available RX buffers/strides
        // in the RX ring.
        status = rmx_input_get_next_chunk(&m_chunk_handle);
        if (status == RMX_CHECKSUM_ISSUE) {
            std::cerr << "Error: CRC" << std::endl;
            status = RMX_OK;
        }
        comp = rmx_input_get_chunk_completion(&m_chunk_handle);
        if (rmx_input_get_completion_chunk_size(comp) > 0 || status == RMX_SIGNAL) {
            return status;
        }
        m_event_mgr.request_notification(m_stream_id);
    }

    status = rmx_input_get_next_chunk(&m_chunk_handle);
    if (status == RMX_CHECKSUM_ISSUE) {
        std::cerr << "Error: CRC" << std::endl;
        status = RMX_OK;
    }
    if (status != RMX_OK && status != RMX_SIGNAL) {
        std::cerr << "Failed to get next chunk. Error: " << status << std::endl;
    }

    comp = rmx_input_get_chunk_completion(&m_chunk_handle);

    return status;
}

void RxStream::configure_stream()
{
    rmx_input_set_mem_capacity_in_packets(&m_stream_params, m_buffer_elements);

    // If HDS is needed, 2 sub block will be configured
    if (is_hds_used()) {
        rmx_input_set_mem_sub_block_count(&m_stream_params, 2);

        // Configure header block
        rmx_input_set_entry_size_range(&m_stream_params, m_header_mem_block_id, m_header_size, m_header_size);
    }
    else {
        rmx_input_set_mem_sub_block_count(&m_stream_params, 1);
    }

    // Always configure payload block
    rmx_input_set_entry_size_range(&m_stream_params, m_payload_mem_block_id, m_payload_size, m_payload_size);

    // Enable per packet information for what's provided by completion
    rmx_input_enable_stream_option(&m_stream_params, RMX_INPUT_STREAM_CREATE_INFO_PER_PACKET);

    rmx_input_set_timestamp_format(&m_stream_params, m_timestamp_format);

    // Configure interface to be used for this stream
    rmx_input_set_stream_nic_address(&m_stream_params, reinterpret_cast<sockaddr*>(&m_addr));
}

bool RxStream::stream_initialize()
{
    // Initialize stream builder first
    rmx_input_init_stream(&m_stream_params, m_rx_type);

    // Configure desired stream attributes
    configure_stream();

    // Allocate memory for the stream.
    bool status = allocate_memory();
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

bool RxStream::main_loop()
{
    // Check if user set cpu affinity
    if (m_cpu_affinity.size() > 0) {
        rt_set_thread_affinity(m_cpu_affinity);
    }

    auto start_time = high_resolution_clock::now();
    while (!exit_app()) {
        // Get the next chunk of packets from the stream.
        const rmx_input_completion *comp;
        const rmx_status status = get_next_chunk(comp);
        if (status == RMX_SIGNAL) {
            return true;
        }
        if (status != RMX_OK) {
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

void RxStream::check_packets_drop(uint32_t sequence)
{
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

void RxStream::process_packets(const rmx_input_completion *comp)
{
    // Retrieve base pointers for this completion
    const uint8_t* data_ptr = reinterpret_cast<const uint8_t*>(rmx_input_get_completion_ptr(comp, m_payload_mem_block_id));

    // If there's a separate header buffer, then the header will be written to that buffer.
    // Otherwise, the header will precede the data in the payload buffer.
    const uint8_t* header_ptr = nullptr;
    if (is_hds_used()) {
        header_ptr = reinterpret_cast<const uint8_t*>(rmx_input_get_completion_ptr(comp, m_header_mem_block_id));
    }

    const size_t chunk_size = rmx_input_get_completion_chunk_size(comp);
    for (size_t packet_index = 0; packet_index < chunk_size; ++packet_index) {
        const rmx_input_packet_info* packet_info = rmx_input_get_packet_info(&m_chunk_handle, packet_index);
        const size_t payload_size = rmx_input_get_packet_size(packet_info, m_payload_mem_block_id);
        const uint8_t* data = data_ptr;
        size_t header_size = 0;

        if (is_hds_used()) {
            header_size = rmx_input_get_packet_size(packet_info, m_header_mem_block_id);
        } else {
            header_ptr = data_ptr;
            data += header_size;
        }

        // Update the statistics.
        m_statistics.received_packets++;
        m_statistics.received_bytes += payload_size;
        m_statistics.received_bytes += header_size;

        if (m_use_checksum_header) {
            ChecksumHeader *hdr = (ChecksumHeader*)header_ptr;

            check_packets_drop(ntohl(hdr->sequence));

            // Calculate and compare the packet checksum.
            uint32_t checksum = ntohl(hdr->checksum);
            if (m_gpu == GPU_ID_INVALID) {
                host_compare_checksum(checksum, data, payload_size);
            } else {
                gpu_compare_checksum(checksum, const_cast<uint8_t*>(data), payload_size, m_statistics.gpu_checksum_mismatch);
            }
        }

        // Increment source data pointers
        data_ptr += m_data_stride_size;
        if (is_hds_used()) {
            header_ptr += m_header_stride_size;
        }
    }
}

void RxStream::host_compare_checksum(uint32_t expected, const uint8_t *data, size_t size)
{
    uint32_t checksum = 0;
    for (size_t i = 0; i < size; i++) {
        checksum += (unsigned char)data[i];
    }
    if (checksum != expected) {
        m_statistics.checksum_mismatch++;
    }
}

void RxStream::update_statistics(high_resolution_clock::time_point& start_time)
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

/**
 * Structure holding arguments required to do generic_receiver logic
 */
struct GenericReceiverArgs
{
    std::string local_ip;
    std::string dst_ip;
    std::string src_ip;
    uint16_t port;
    uint16_t header_size = 0;
    uint16_t payload_size = 1500;
    uint32_t buffer_elements = 1 << 16;
    uint16_t flow_id = 1;
    int gpu = GPU_ID_INVALID;
    bool use_checksum_header = false;
    bool wait_for_event = false;
    std::vector<int> cpu_affinity;
};

bool run(const GenericReceiverArgs& args)
{
    // Create stream.
    sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_port = htons(args.port);
    int rc = inet_pton(AF_INET, args.local_ip.c_str(), &local_addr.sin_addr);
    if (rc != 1) {
        std::cerr << "Failed to parse local network address" << std::endl;
        return false;
    }

    bool status = false;

    if (args.gpu != GPU_ID_INVALID) {

        status = gpu_init(args.gpu);
        if (!status) {
            return false;
        }
    }

    // If special checksum header is needed, set specific header size for it.
    uint16_t expected_header_size = args.header_size;
    if (args.use_checksum_header) {
        expected_header_size = sizeof(ChecksumHeader);
    }

    std::unique_ptr<RxStream> p_stream = std::make_unique<RxStream>(RMX_INPUT_APP_PROTOCOL_PACKET,
                                      RMX_INPUT_TIMESTAMP_RAW_NANO,
                                      args.buffer_elements, args.payload_size, expected_header_size,
                                      local_addr, args.gpu, args.wait_for_event,
                                      args.use_checksum_header, args.cpu_affinity);
    if (!p_stream ) {
        std::cerr << "Failed to create stream." << std::endl;
        return false;
    }

    status = p_stream->stream_initialize();
    if (!status) {
        std::cerr << "Failed initializing stream." << std::endl;
        return false;
    }

    // Prepare flow attributes.
    sockaddr_in destination_address;
    memset(&destination_address, 0, sizeof(destination_address));
    destination_address.sin_port = htons(args.port);
    destination_address.sin_family = AF_INET;
    rc = inet_pton(AF_INET, args.dst_ip.c_str(), &destination_address.sin_addr);
    if (rc != 1) {
        std::cerr << "Failed to parse destination network address" << std::endl;
        return false;
    }

    // Where the stream will be coming from, if specified
    sockaddr_in remote_address;
    memset(&remote_address, 0, sizeof(remote_address));
    remote_address.sin_family = AF_INET;
    rc = inet_pton(AF_INET, args.src_ip.c_str(), &remote_address.sin_addr);
    if (rc != 1) {
        std::cerr << "Failed to parse source network address" << std::endl;
        return false;
    }

    // Attach flow to stream.
    if (!p_stream->attach_flow(destination_address, remote_address, args.flow_id)) {
        std::cerr << "Failed to attach flow to stream." << std::endl;
        return false;
    }

    // Initialize wait mode, if requested.
    if (!p_stream->init_wait()) {
        std::cerr << "Event channel initialization error." << std::endl;
        return false;
    }

    // Run the main loop.
    std::cout << "Running main receive loop..." << std::endl;
    status = p_stream->main_loop();
    if (!status) {
        std::cerr << "Failure in main receive loop." << std::endl;
        return false;
    } else {
        std::cout << "Main receive loop completed; exiting." << std::endl;
    }

    if (args.gpu != GPU_ID_INVALID) {
        gpu_uninit(args.gpu);
    }

    return true;
}

int main(int argc, char *argv[])
{
    GenericReceiverArgs args;

    CLI::App app{"Mellanox Rivermax Generic RX Demo App"};
    app.add_option("-i,--interface-ip", args.local_ip, "IP of the local interface")->required()->check(CLI::ValidIPV4);
    app.add_option("-m,--dst-address", args.dst_ip, "Destination address to bind to")->required()->check(CLI::ValidIPV4);
    app.add_option("-s,--src-address", args.src_ip, "Source address to read from")->required()->check(CLI::ValidIPV4);
    app.add_option("-p,--port", args.port, "Receive port to use")->required()->check(CLI::Range(1, 65535));
    auto *opt_checksum = app.add_flag("-x,--checksum-header", args.use_checksum_header, "Use checksum header");
    app.add_option("-r,--header-size", args.header_size, "User header size", true)->check(CLI::PositiveNumber)->excludes(opt_checksum);
    app.add_option("-d,--data-size", args.payload_size, "User data (payload) size", true)->check(CLI::PositiveNumber);
    app.add_option("-k,--packets", args.buffer_elements, "Number of packets to allocate memory for", true)->check(CLI::PositiveNumber);
    app.add_option("-f,--flow-id", args.flow_id, "Flow id to use", true)->check(CLI::PositiveNumber);
    app.add_flag("-w,--wait-event", args.wait_for_event, "Wait for packets instead of busy loop");
#ifdef CUDA_ENABLED
    app.add_option("-g,--gpu", args.gpu, "GPU to use for GPUDirect (default doesn't use GPU)", true);
#endif
    app.add_option("-a,--cpu-affinity", args.cpu_affinity,
        "Comma separated list of CPU affinity cores for the application main thread."
        )->delimiter(',')->check(CLI::Range(CPU_NONE, MAX_CPU_RANGE));

    CLI11_PARSE(app, argc, argv);

    // Print the library and app version.
    const char *rmax_version = rmx_get_version_string();
    static std::string app_version =
        std::to_string(RMX_VERSION_MAJOR) + std::string(".") +
        std::to_string(RMX_VERSION_MINOR) + std::string(".") +
        std::to_string(RMX_VERSION_PATCH);
    std::cout << "#########################################\n";
    std::cout << "## Rivermax SDK version:        " << rmax_version << "\n";
    std::cout << "## Generic Receiver version:    " << app_version << "\n";
    std::cout << "#########################################\n";

    // Initializes signals caught by the application
    initialize_signals();

    // Initialize Rivermax library.
    rmx_status rmax_status = rmx_enable_system_signal_handling();
    if (rmax_status != RMX_OK) {
        std::cerr << "Failed to enable system signal handling; error: " << rmax_status << std::endl;
        exit(EXIT_FAILURE);
    }

    rmax_status = rmx_init();
    if (rmax_status != RMX_OK) {
        std::cerr << "Failed initializing Rivermax; error: " << rmax_status << std::endl;
        exit(EXIT_FAILURE);
    }

    bool has_succeeded = run(args);

    rmax_status = rmx_cleanup();
    if (rmax_status != RMX_OK) {
        std::cerr << "Failed to clean up Rivermax; error: " << rmax_status << std::endl;
        has_succeeded = false;
    }

    return has_succeeded ? EXIT_SUCCESS : EXIT_FAILURE;
}
