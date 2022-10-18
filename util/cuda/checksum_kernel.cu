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

#include <stdio.h>

static const int blockSize = 1024;

__global__ void 
cuda_compare_checksum_kernel(unsigned int expected, unsigned char* data,
                             unsigned int size, unsigned int* mismatches)
{
    int idx = threadIdx.x;

    // Calculate the sum for each thread.
    int sum = 0;
    for (int i = idx; i < size; i += blockSize)
        sum += data[i];

    __shared__ unsigned int accum[blockSize];
    accum[idx] = sum;

    // Reduce the sums of all blocks.
    __syncthreads();
    for (int size = blockSize / 2; size > 0; size /= 2) {
        if (idx < size)
            accum[idx] += accum[idx + size];
        __syncthreads();
    }

    // Output the results in the first thread.
    if (idx == 0 && accum[0] != expected)
        *mismatches = *mismatches + 1;
}

extern "C"
void cuda_compare_checksum(unsigned int expected, unsigned char* data,
                           unsigned int size, unsigned int* mismatches)
{
    cuda_compare_checksum_kernel<<<1, blockSize>>>(expected, data, size, mismatches);
}
