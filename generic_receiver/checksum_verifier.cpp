/*
 * SPDX-FileCopyrightText: Copyright (c) 2025-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include "checksum_verifier.h"

#include <iostream>
#include <stdlib.h>

#include "gpu.h"

ChecksumVerifier::ChecksumVerifier(const std::shared_ptr<MemoryAllocator>& allocator)
    : m_mem_allocator(allocator)
{
}

CPUChecksumVerifier::CPUChecksumVerifier(const std::shared_ptr<MemoryAllocator>& allocator)
    : ChecksumVerifier(allocator)
{
}

bool CPUChecksumVerifier::initialize()
{
    return true;
}

void CPUChecksumVerifier::add_packet(const uint8_t* data, size_t size, uint32_t expected_checksum)
{
    // Todo: Improve checksum processing
    // Either change how it's done, use hardware offloading if possible
    // Single thread can't handle max bandwidth
    uint32_t checksum = 0;
    for (size_t i = 0; i < size; i++) {
        checksum += data[i];
    }

    if (checksum != expected_checksum) {
        m_mismatch_count++;
    }
}

void CPUChecksumVerifier::complete_batch()
{
    // Nothing special for now
}

uint32_t CPUChecksumVerifier::get_and_reset_mismatch_count()
{
    return m_mismatch_count;
}

GPUChecksumVerifier::GPUChecksumVerifier(size_t max_batch_size,
                                         const std::shared_ptr<MemoryAllocator>& allocator)
    : ChecksumVerifier(allocator)
    , m_max_batch_size(max_batch_size)
{
    m_packet_ptrs.reserve(m_max_batch_size);
    m_packet_sizes.reserve(m_max_batch_size);
    m_expected_checksums.reserve(m_max_batch_size);
}

bool GPUChecksumVerifier::initialize()
{
    m_gpu_packet_ptrs = reinterpret_cast<const uint8_t**>(
        m_mem_allocator->allocate(m_max_batch_size * sizeof(size_t)));
    m_gpu_packet_sizes =
        reinterpret_cast<size_t*>(m_mem_allocator->allocate(m_max_batch_size * sizeof(size_t)));
    m_gpu_expected_checksums =
        reinterpret_cast<uint32_t*>(m_mem_allocator->allocate(m_max_batch_size * sizeof(uint32_t)));
    m_gpu_mismatch_count = reinterpret_cast<uint32_t*>(m_mem_allocator->allocate(sizeof(uint32_t)));

    if (m_gpu_packet_ptrs == nullptr || m_gpu_packet_sizes == nullptr ||
        m_gpu_expected_checksums == nullptr || m_gpu_mismatch_count == nullptr) {
        std::cout << "Failed to allocate ressources for GPU ChecksumVerifier" << std::endl;
        return false;
    }

    gpu_reset_counter(m_gpu_mismatch_count);
    return true;
}

void GPUChecksumVerifier::add_packet(const uint8_t* data, size_t size, uint32_t expected_checksum)
{
    m_packet_ptrs.push_back(data);
    m_packet_sizes.push_back(size);
    m_expected_checksums.push_back(expected_checksum);
}

void GPUChecksumVerifier::complete_batch()
{
    std::shared_ptr<MemoryUtils> mem_utils = m_mem_allocator->get_memory_utils();
    mem_utils->memory_copy(m_gpu_packet_ptrs, m_packet_ptrs.data(),
                           m_packet_ptrs.size() * sizeof(const uint8_t*));
    mem_utils->memory_copy(m_gpu_packet_sizes, m_packet_sizes.data(),
                           m_packet_sizes.size() * sizeof(size_t));
    mem_utils->memory_copy(m_gpu_expected_checksums, m_expected_checksums.data(),
                           m_expected_checksums.size() * sizeof(uint32_t));

    // Launch the kernel
    gpu_compare_checksum(m_gpu_packet_ptrs, m_gpu_packet_sizes, m_gpu_expected_checksums,
                         m_gpu_mismatch_count, static_cast<uint32_t>(m_packet_ptrs.size()));

    m_packet_ptrs.clear();
    m_packet_sizes.clear();
    m_expected_checksums.clear();
}

uint32_t GPUChecksumVerifier::get_and_reset_mismatch_count()
{
    const uint32_t value = gpu_read_counter(m_gpu_mismatch_count);
    gpu_reset_counter(m_gpu_mismatch_count);
    return value;
}
