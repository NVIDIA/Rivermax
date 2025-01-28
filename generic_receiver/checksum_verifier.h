/*
 * SPDX-FileCopyrightText: Copyright (c) 2025-2025 AFFILIATES. All rights reserved.
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

#include "memory_allocator.h"

class ChecksumVerifier
{
public:
    ChecksumVerifier(const std::shared_ptr<MemoryAllocator>& allocator);
    virtual ~ChecksumVerifier() = default;

public:
    /**
     * Used to initialize the verifier internals.
     * @return true if initialization is successful, false otherwise.
     */
    virtual bool initialize() = 0;

    /**
     * Adds a packet that needs checksum verification.
     * @param data The packet data address.
     * @param size The size of the packet data.
     * @param expected The expected packet checksum (for verification purposes).
     */
    virtual void add_packet(const uint8_t* data, size_t size, uint32_t expected_checksum) = 0;

    /**
     * Signal the end of a batch and trigger any necessary processing.
     */
    virtual void complete_batch() = 0;

    /**
     * Get the count of mismatched packets and reset the counter.
     * @return The number of mismatched packets.
     */
    virtual uint32_t get_and_reset_mismatch_count() = 0;

protected:
    std::shared_ptr<MemoryAllocator> m_mem_allocator;
};

class CPUChecksumVerifier : public ChecksumVerifier
{
public:
    CPUChecksumVerifier(const std::shared_ptr<MemoryAllocator>& allocator);

public:
    virtual bool initialize() override;
    virtual void add_packet(const uint8_t* data, size_t size,
                            uint32_t expected_checksum) override;
    virtual void complete_batch() override;
    virtual uint32_t get_and_reset_mismatch_count() override;

protected:
    uint32_t m_mismatch_count = 0;
};

class GPUChecksumVerifier : public ChecksumVerifier
{
public:
    GPUChecksumVerifier(size_t max_batch_size, const std::shared_ptr<MemoryAllocator>& allocator);

public:
    virtual bool initialize() override;
    virtual void add_packet(const uint8_t* data, size_t size,
                            uint32_t expected_checksum) override;
    virtual void complete_batch() override;
    virtual uint32_t get_and_reset_mismatch_count() override;

protected:
    size_t m_max_batch_size = 0;
    std::vector<const uint8_t*> m_packet_ptrs;
    std::vector<size_t> m_packet_sizes;
    std::vector<uint32_t> m_expected_checksums;

    const uint8_t** m_gpu_packet_ptrs = nullptr;
    size_t* m_gpu_packet_sizes = nullptr;
    uint32_t* m_gpu_expected_checksums = nullptr;
    uint32_t* m_gpu_mismatch_count = nullptr;
};
