/*
 * SPDX-FileCopyrightText: Copyright (c) 2022-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#ifndef _GENERIC_RECEIVER_GPU_H_
#define _GENERIC_RECEIVER_GPU_H_

#include "defs.h"

#define GPU_ID_INVALID -1
constexpr const char* CUDA_DEVICE_ORDER = "CUDA_DEVICE_ORDER";
constexpr const char* CUDA_PCI_BUS_ID_DEVICE_ORDER = "PCI_BUS_ID";

/**
 * @brief BAR1 Memory allocation information for a device
 *
 */
typedef struct gpu_bar1_memory_info {
    uint64_t free; /**< Unallocated BAR1 Memory (in bytes) */
    uint64_t total; /**< Total BAR1 Memory (in bytes) */
    uint64_t used; /**< Allocated Used Memory (in bytes) */
} gpu_bar1_memory_info;

typedef enum {
    GPU_DEFAULT = (1ul << 0),
    GPU_SET_MAX_CLOCK_FREQUENCY = (1ul << 1), /**< set GPU and memory clock to lock on max frequency */
} gpu_init_config_flags;

/**
 * @brief Configuration for initialize GPU
 *
 */
typedef struct gpu_init_config {
    uint64_t flags; /**< Configuration flags, using bits from @ref gpu_init_config_flags */
} gpu_init_config;

#ifdef CUDA_ENABLED
#include <iostream>
#include <cuda_runtime.h>
#include <cuda.h>

bool gpu_init(int gpu_id, gpu_init_config& init_config);
bool gpu_uninit(int gpu_id, gpu_init_config& init_config);
bool verify_gpu_device_id(int device_id);
const std::string get_gpu_device_name(int device_id);
size_t gpu_align_physical_allocation_size(int gpu_id, size_t acllocation_size);
uint32_t* gpu_allocate_counter();
uint32_t gpu_read_counter(uint32_t *counter);
void gpu_reset_counter(uint32_t *counter);
void* gpu_allocate_memory(int gpu_id, size_t size, size_t align);
bool gpu_free_memory(void* ptr, size_t size);
bool gpu_memset(void* dst, int value, size_t count);
bool gpu_memcpy(void* dst, const void* src, size_t count);
void gpu_compare_checksum(uint32_t expected, unsigned char* data, size_t size, uint32_t* mismatches);
bool set_gpu_device(int gpu_id);
#ifndef TEGRA_ENABLED
bool gpu_set_locked_clocks_max_freq(int gpu_id);
bool gpu_reset_locked_clocks(int gpu_id);
bool gpu_query_bar1_memory_info(int gpu_id, gpu_bar1_memory_info& mem_info);
bool gpu_verify_allocated_bar1_size(int gpu_id, size_t size);
void* cudaAllocateMmap(int gpu_id, size_t size, size_t align);
bool cudaFreeMmap(uint64_t* ptr, size_t size);
#else // TEGRA_ENABLED
static inline bool gpu_set_locked_clocks_max_freq(int gpu_id)
{
    NOT_IN_USE(gpu_id);
    return true;
}
static inline bool gpu_reset_locked_clocks(int gpu_id)
{
    NOT_IN_USE(gpu_id);
    return true;
}
static inline bool gpu_query_bar1_memory_info(int gpu_id, gpu_bar1_memory_info& mem_info)
{
    NOT_IN_USE(gpu_id);
    NOT_IN_USE(mem_info);
    return true;
}
static inline bool gpu_verify_allocated_bar1_size(int gpu_id, size_t size)
{
    NOT_IN_USE(gpu_id);
    NOT_IN_USE(size);
    return true;
}
#endif
#else // !CUDA_ENABLED

static inline bool gpu_init(int gpu_id, gpu_init_config& init_config)
{
    NOT_IN_USE(gpu_id);
    NOT_IN_USE(init_config);
    return false;
}

static inline bool gpu_uninit(int gpu_id, gpu_init_config& init_config)
{
    NOT_IN_USE(gpu_id);
    NOT_IN_USE(init_config);
    return false;
}

static inline const std::string get_gpu_device_name(int device_id)
{
    NOT_IN_USE(device_id);
    return "";
}

static inline size_t gpu_align_physical_allocation_size(int gpu_id, size_t acllocation_size)
{
    NOT_IN_USE(gpu_id);
    return acllocation_size;
}

static inline uint32_t* gpu_allocate_counter()
{
    return nullptr;
}

static inline uint32_t gpu_read_counter(uint32_t *counter)
{
    NOT_IN_USE(counter);
    return 0;
}

static inline void gpu_reset_counter(uint32_t *counter)
{
    NOT_IN_USE(counter);
}

static inline void* gpu_allocate_memory(int gpu, size_t size, size_t align)
{
    NOT_IN_USE(gpu);
    NOT_IN_USE(size);
    NOT_IN_USE(align);
    return nullptr;
}

static inline bool gpu_free_memory(void* ptr, size_t size)
{
    NOT_IN_USE(ptr);
    NOT_IN_USE(size);
    return false;
}

static inline bool gpu_memset(void* dst, int value, size_t count)
{
    NOT_IN_USE(dst);
    NOT_IN_USE(value);
    NOT_IN_USE(count);

    return false;
}

static inline bool gpu_memcpy(void* dst, const void* src, size_t count)
{
    NOT_IN_USE(dst);
    NOT_IN_USE(src);
    NOT_IN_USE(count);

    return false;
}

static inline void* cudaAllocateMmap(int gpu_id, size_t size, size_t align)
{
    NOT_IN_USE(gpu_id);
    NOT_IN_USE(size);
    NOT_IN_USE(align);
    return nullptr;
}

static inline bool cudaFreeMmap(uint64_t* ptr, size_t size)
{
    NOT_IN_USE(ptr);
    NOT_IN_USE(size);
    return false;
}

static inline bool verify_gpu_device_id(int gpu)
{
    NOT_IN_USE(gpu);
    return true;
}

static inline void gpu_compare_checksum(uint32_t expected, unsigned char* data, size_t size, uint32_t* mismatches)
{
    NOT_IN_USE(expected);
    NOT_IN_USE(data);
    NOT_IN_USE(size);
    NOT_IN_USE(mismatches);
}

static inline bool set_gpu_device(int gpu_id)
{
    NOT_IN_USE(gpu_id);

    return false;
}

static inline bool gpu_set_locked_clocks_max_freq(int gpu_id)
{
    NOT_IN_USE(gpu_id);
    return false;
}

static inline bool gpu_reset_locked_clocks(int gpu_id)
{
    NOT_IN_USE(gpu_id);
    return false;
}

static inline bool gpu_query_bar1_memory_info(int gpu_id, gpu_bar1_memory_info& mem_info)
{
    NOT_IN_USE(gpu_id);
    NOT_IN_USE(mem_info);
    return false;
}

static inline bool gpu_verify_allocated_bar1_size(int gpu_id, size_t size)
{
    NOT_IN_USE(gpu_id);
    NOT_IN_USE(size);
    return false;
}

#endif // CUDA_ENABLED
#endif // _GENERIC_RECEIVER_GPU_H_

