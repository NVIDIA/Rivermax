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

#include <cuda_runtime.h>
#include <stdio.h>
#include <stdint.h>

static constexpr int threads_per_block = 256;
static constexpr size_t shared_mem_size = threads_per_block * sizeof(uint32_t);

__global__ void cuda_compare_checksum_kernel(const uint8_t** __restrict__ packet_ptrs,
                                             const size_t* __restrict__ packet_lengths,
                                             const uint32_t* __restrict__ expected_checksums,
                                             unsigned int* __restrict__ mismatch_counter,
                                             const int num_packets)
{
    // Each block will process one packet
    // while each thread will process a portion of the packet
    int packet_idx = blockIdx.x;

    if (packet_idx < num_packets) {
        extern __shared__ uint32_t shared_data[];

        // Get packet information
        const uint8_t* data = packet_ptrs[packet_idx];
        const size_t length = packet_lengths[packet_idx];

        uint32_t partial_sum = 0;
        unsigned int thread_id = threadIdx.x;
        unsigned int num_threads = blockDim.x;

        for (size_t i = thread_id; i < length; i += num_threads) {
            partial_sum += data[i];
        }

        shared_data[thread_id] = partial_sum;
        __syncthreads();

        // Reduce the sums of all blocks.
        for (unsigned int size = blockDim.x / 2; size > 0; size >>= 1) {
            if (thread_id < size) {
                shared_data[thread_id] += shared_data[thread_id + size];
            }
            __syncthreads();
        }

        // The first thread checks the checksum
        if (thread_id == 0) {
            const uint32_t computed_checksum = shared_data[0];
            const uint32_t expected_checksum = expected_checksums[packet_idx];

            if (computed_checksum != expected_checksum) {
                atomicAdd(mismatch_counter, 1);
            }
        }
    }
}

extern "C" void cuda_compare_checksum(const uint8_t** data_ptrs, const size_t* sizes,
                                      const uint32_t* expected_checksums,
                                      uint32_t* mismatch_counter, uint32_t num_packet)
{
    cuda_compare_checksum_kernel<<<num_packet, threads_per_block, shared_mem_size>>>(
        data_ptrs, sizes, expected_checksums, mismatch_counter, num_packet);
}
