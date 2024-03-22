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

#ifdef CUDA_ENABLED

#include <iostream>
#include <cstring>
#ifndef TEGRA_ENABLED
#include <nvml.h>
#endif
#include "defs.h"
#include "rt_threads.h"
#include "gpu.h"

extern "C"
void cuda_compare_checksum(unsigned int expected, unsigned char* data,
    unsigned int size, unsigned int* mismatches);

/**
 * @brief: Initialize GPU.
 *
 * @warning This must be called before any other GPU functions call
 *
 * @param [in] gpu_id     : GPU id.
 * @param [in] init_config: GPU init config struct @ref gpu_init_config
 *
 * @return: Return status of the operation.
 */
bool gpu_init(int gpu_id, gpu_init_config& init_config)
{
    int ret = -1;
    // nvidia-smi is the user's primary tool for identifying the ID of the GPU which is to be used by the Rivermax application.
    // The ID returned by nvidia-smi is obtained by its enumerating the GPU devices according to their PCI order.
    // However, by default the CUDA driver and runtime APIs enumerate GPUs according to their speed (and not by PCI order).
    // To align the ID obtained from nvidia-smi and those used by the CUDA driver and runtime APIs we modify their GPU ID enumeration
    // policy by setting the CUDA_DEVICE_ORDER environment variable with value PCI_BUS_ID.
    ret = set_enviroment_variable(CUDA_DEVICE_ORDER, CUDA_PCI_BUS_ID_DEVICE_ORDER);
    if (ret != 0) {
        std::cerr << "Failed to set env variable " << CUDA_DEVICE_ORDER << "="
            << CUDA_PCI_BUS_ID_DEVICE_ORDER << std::endl;
        return false;
    }
    std::cout << "Set env variable " << CUDA_DEVICE_ORDER << "="
        << CUDA_PCI_BUS_ID_DEVICE_ORDER << std::endl;

    if (!verify_gpu_device_id(gpu_id)) {
        return false;
    }
    if (init_config.flags & GPU_SET_MAX_CLOCK_FREQUENCY) {
        if(!gpu_set_locked_clocks_max_freq(gpu_id)) {
            return false;
        }
    }

    return true;
}

/**
 * @brief: Uninitialize GPU.
 * @warning This must be called in the end of using GPU.
 *
 * @param [in] gpu_id     : GPU id.
 * @param [in] init_config: GPU init config @ref gpu_init_config.
 *
 * @return: Return status of the operation.
 */
bool gpu_uninit(int gpu_id, gpu_init_config& init_config)
{
    if (init_config.flags & GPU_SET_MAX_CLOCK_FREQUENCY) {
        return gpu_reset_locked_clocks(gpu_id);
    }
    return true;
}

void gpu_compare_checksum(uint32_t expected, unsigned char* data, size_t size, uint32_t* mismatches)
{
    cuda_compare_checksum(expected, data, (uint32_t)size, mismatches);
}

uint32_t* gpu_allocate_counter()
{
    uint32_t *counter;
    cudaMalloc(&counter, sizeof(uint32_t));
    cudaMemset(counter, 0, sizeof(uint32_t));
    return counter;
}

uint32_t gpu_read_counter(uint32_t *counter)
{
    uint32_t result;
    cudaMemcpy(&result, counter, sizeof(uint32_t), cudaMemcpyDeviceToHost);
    return result;
}

void gpu_reset_counter(uint32_t *counter)
{
    cudaMemset(counter, 0, sizeof(uint32_t));
}

/**
 * @brief: Allocates GPU memory, support both Tegra (Page pinned shared memory)
 *         and non Tegara (Device memory) GPUs.
 *
 * @param [in] gpu_id: GPU id.
 * @param [in] size  : Size of the memory to allocate.
 * @param [in] align : Alignment of the reserved virtual address size requested.
 *
 * @return: Pointer to the allocated memory.
 */
void* gpu_allocate_memory(int gpu_id, size_t size, size_t align)
{
    int count;
    cudaError_t cuda_err = cudaGetDeviceCount(&count);
    if (cuda_err != cudaSuccess || count <= gpu_id) {
        std::cerr << "Failed to allocate GPU memory; GPU " << gpu_id << " not available." << std::endl;
        return nullptr;
    }

    cudaDeviceProp props;
    cuda_err = cudaGetDeviceProperties(&props, gpu_id);
    if (cuda_err != cudaSuccess || !props.canMapHostMemory) {
        std::cerr << "Failed to allocate GPU memory; host mapping not supported." << std::endl;
        return nullptr;
    }

    cuda_err = cudaSetDevice(gpu_id);
    if (cuda_err != cudaSuccess) {
        std::cerr << "Failed to allocate GPU memory; failed to set device." << std::endl;
        return nullptr;
    }

#ifdef TEGRA_ENABLED
    cuda_err = cudaSetDeviceFlags(cudaDeviceMapHost);
    if (cuda_err != cudaSuccess) {
        std::cerr << "Failed to allocate GPU memory; failed to set device flags." << std::endl;
        return nullptr;
    }
#endif

    char* buffer;
#ifdef TEGRA_ENABLED
    cuda_err = cudaMallocHost((void**)&buffer, size);
#else
    buffer = (char*)cudaAllocateMmap(gpu_id, size, align);
#endif
    if (cuda_err != cudaSuccess || buffer == nullptr) {
        std::cerr << "Failed to allocate GPU memory." << std::endl;
        return nullptr;
    }

#ifndef TEGRA_ENABLED
    unsigned int flag = 1;
    cuPointerSetAttribute(&flag, CU_POINTER_ATTRIBUTE_SYNC_MEMOPS, (CUdeviceptr)buffer);
#endif
    cuda_err = cudaDeviceSynchronize();
    if (cuda_err != cudaSuccess) {
        std::cerr << "Failed to allocate GPU memory; failed to synchronize. error: " << cuda_err << std::endl;
        return nullptr;
    }

    std::cout << "GPU allocation succeeded, GPU id = " << gpu_id << " ,size = " << size << std::endl;
    return buffer;
}

/**
 * @brief: Free GPU memory, support both Tegra (Page pinned shared memory)
 *         and non Tegara (Device memory) GPUs.
 *
 * @param [in] ptr: Memory address to free
 *
 * @return: Return status of the operation.
 */
bool gpu_free_memory(void* ptr, size_t size)
{
    cudaError_t cuda_err = cudaSuccess;

#ifdef TEGRA_ENABLED
    NOT_IN_USE(size);
    cuda_err = cudaFreeHost (ptr);
#else
    //cuda_err = cudaFree (ptr);
    return cudaFreeMmap((uint64_t*)&ptr, size);
#endif
    if (cuda_err != cudaSuccess) {
        std::cerr << "Failed to free GPU memory, ret " << cuda_err << std::endl;
        return false;
    }

    return true;
}

size_t gpu_query_alignment(int gpu_id)
{
#ifdef TEGRA_ENABLED
    return 1;
#else // TEGRA_ENABLED
    CUresult status = CUDA_SUCCESS;
    size_t granularity = 0;
    CUmemAllocationProp prop = {};
    prop.type = CU_MEM_ALLOCATION_TYPE_PINNED;
    prop.location.type = CU_MEM_LOCATION_TYPE_DEVICE;
    prop.location.id = gpu_id;
    // Get the minimum granularity needed for the resident devices
    // (the max of the minimum granularity of each participating device)
    status = cuMemGetAllocationGranularity(&granularity, &prop, CU_MEM_ALLOC_GRANULARITY_MINIMUM);
    if (status != CUDA_SUCCESS) {
        std::cout << "cuMemGetAllocationGranularity failed status = " << status << "\n";
        return 1;
    }
    return granularity;
#endif
}

size_t gpu_align_physical_allocation_size(int gpu_id, size_t allocation_size)
{
    size_t size = allocation_size;
    size_t granularity = gpu_query_alignment(gpu_id);
    // Round up the size such that we can evenly split it into a stripe size that
    // meets the granularity requirements Essentially size = N *
    // residentDevices.size() * min_granularity is the requirement, since each
    // piece of the allocation will be stripeSize = N * min_granularity and the
    // min_granularity requirement applies to each stripeSize piece of the
    // allocation.
    size = round_up(allocation_size, granularity); /* This must always co-exist with the NIC size restrictions. Is it guaranteed to? */
    return size;
}

#ifndef TEGRA_ENABLED
void* cudaAllocateMmap(int gpu_id, size_t size, size_t align)
{
    CUresult status = CUDA_SUCCESS;
    CUdeviceptr dptr = 0;
    int val = 0;
    std::cout << "CUDA memory allocation on GPU - cuMemCreate " << std::endl;

    // Setup the properties common for all the chunks
    // The allocations will be device pinned memory.
    // This property structure describes the physical location where the memory
    // will be allocated via cuMemCreate along with additional properties In this
    // case, the allocation will be pinned device memory local to a given device.
    CUmemAllocationProp prop = {};
    prop.type = CU_MEM_ALLOCATION_TYPE_PINNED;
    prop.location.type = CU_MEM_LOCATION_TYPE_DEVICE;
    prop.location.id = gpu_id;

    status = cuDeviceGetAttribute(&val, CU_DEVICE_ATTRIBUTE_VIRTUAL_MEMORY_MANAGEMENT_SUPPORTED, prop.location.id);
    if (status != CUDA_SUCCESS || val == 0) {
        std::cout << "Device does not support VA. status = " << status << "\n";
        goto done;
    }

    status = cuDeviceGetAttribute(&val, CU_DEVICE_ATTRIBUTE_GPU_DIRECT_RDMA_WITH_CUDA_VMM_SUPPORTED, prop.location.id);
    if (status != CUDA_SUCCESS || val == 0) {
        std::cout << "RDMA is not supported or not enabled, status = " << status << " val = " << val << "\n";
        goto done;
    } else {
        std::cout << "RDMA is supported and enabled, status \n";
        prop.allocFlags.gpuDirectRDMACapable = 1;
    }

    // Reserve the required contiguous VA space for the allocations
    status = cuMemAddressReserve(&dptr, size, align, 0, 0);
    if (status != CUDA_SUCCESS) {
        std::cout << "cuMemAddressReserve failed status = " << status << "\n";
        goto done;
    }

    // Create the allocation as a pinned allocation on this device
    CUmemGenericAllocationHandle allocationHandle;
    status = cuMemCreate(&allocationHandle, size, &prop, 0);
    if (status != CUDA_SUCCESS) {
        std::cout << "cuMemCreate failed status = " << status << "\n";
        goto done;
    }

    // Assign the chunk to the appropriate VA range and release the handle.
    // After mapping the memory, it can be referenced by virtual address.
    // Since we do not need to make any other mappings of this memory or export
    // it, we no longer need and can release the allocationHandle. The
    // allocation will be kept live until it is unmapped.
    status = cuMemMap(dptr, size, 0, allocationHandle, 0);

    // the handle needs to be released even if the mapping failed.
    status = cuMemRelease(allocationHandle);
    if (status != CUDA_SUCCESS) {
        std::cout << "cuMemRelease failed status = " << status << "\n";
        goto done;
    }
    // Each accessDescriptor will describe the mapping requirement for a single
    // device
    CUmemAccessDesc accessDescriptors;

    // Prepare the access descriptor array indicating where and how the backings
    // should be visible.
    // Specify which device we are adding mappings for.
    accessDescriptors.location.type = CU_MEM_LOCATION_TYPE_DEVICE;

    accessDescriptors.location.id = gpu_id;

    // Specify both read and write access.
    accessDescriptors.flags = CU_MEM_ACCESS_FLAGS_PROT_READWRITE;

    // Apply the access descriptors to the whole VA range.
    status = cuMemSetAccess(dptr, size, &accessDescriptors, 1);
    if (status != CUDA_SUCCESS) {
        std::cout << "cuMemSetAccess failed status = " << status << "\n";
        goto done;
    }

    std::cout << "CUDA memory allocation on GPU - cuMemCreate Done" << std::endl;

done:
    if (status != CUDA_SUCCESS) {
        bool cuda_free_mmap_status;

        cuda_free_mmap_status = cudaFreeMmap((uint64_t*)&dptr, size);
        std::cout << "CUDA memory free finished with status " << cuda_free_mmap_status << std::endl;
        return nullptr;
    }
    
    return (void*)dptr;
}

bool cudaFreeMmap(uint64_t *ptr, size_t size)
{
    if (!ptr) {
        return true;
    }
    std::cout << "CUDA cudaFreeMmap " << std::hex << *ptr << std::dec << std::endl;
    CUdeviceptr dptr = *(CUdeviceptr*)ptr;
    CUresult status = CUDA_SUCCESS;

    // Unmap the mapped virtual memory region
    // Since the handles to the mapped backing stores have already been released
    // by cuMemRelease, and these are the only/last mappings referencing them,
    // The backing stores will be freed.
    // Since the memory has been unmapped after this call, accessing the specified
    // va range will result in a fault (until it is re-mapped).
    status = cuMemUnmap(dptr, size);
    if (status != CUDA_SUCCESS) {
        std::cout << "CUDA cuMemUnmap failed " << status << std::endl;
        return false;
    }
    // Free the virtual address region.  This allows the virtual address region
    // to be reused by future cuMemAddressReserve calls.  This also allows the
    // virtual address region to be used by other allocation made through
    // Operating system calls like malloc & mmap.
    status = cuMemAddressFree(dptr, size);
    if (status != CUDA_SUCCESS) {
        std::cout << "CUDA cuMemAddressFree failed " << status << std::endl;
        return false;
    }
    return true;
}
#endif

/**
 * @brief: Set GPU memory, support both Tegra (Page pinned shared memory)
 *         and non Tegara (Device memory) GPUs.
 *
 * @param [in] dst: Destination memory address.
 * @param [in] value: Value to set for each byte of specified memory.
 * @param [in] count: Size in bytes to set.
 *
 * @return: Return status of the operation.
 */
bool gpu_memset(void* dst, int value, size_t count)
{
    cudaError_t cuda_err = cudaSuccess;

#ifdef TEGRA_ENABLED
    std::memset(dst, value, count);
#else
    cuda_err = cudaMemset(dst, value, count);
#endif
    if (cuda_err != cudaSuccess) {
        std::cerr << "Failed to set GPU memory." << std::endl;
        return false;
    }

    return true;
}

/**
 * @brief: Copy to/from GPU memory, support both Tegra (Page pinned shared memory)
 *         and non Tegara (Device memory) GPUs.
 *
 * @param [in] dst: Destination memory address.
 * @param [in] src:  Source memory address.
 * @param [in] count: Size in bytes to copy.
 *
 * @return: Return status of the operation.
 */
bool gpu_memcpy(void* dst, const void* src, size_t count)
{
    cudaError_t cuda_err = cudaSuccess;

#ifdef TEGRA_ENABLED
    std::memcpy(dst, src, count);
#else
    cuda_err = cudaMemcpyAsync(dst, src, count, cudaMemcpyDefault);
#endif
    if (cuda_err != cudaSuccess) {
        std::cerr << "Failed to copy memory to/from GPU." << std::endl;
        return false;
    }

    return true;
}

bool verify_gpu_device_id(int device_id)
{
    int count = 0;

    cudaGetDeviceCount(&count);
    // verify user configuration is correct
    if (device_id != GPU_ID_INVALID) {
        if ((device_id >= count) || (0 > device_id)) {
            std::cout << "ERROR: User set the GPU id as = " << device_id << " but maximum allowed GPU id is " << count - 1 << std::endl;
            return false;
        }
        std::cout << "gpu_device_id = " << device_id << std::endl;
    }
    return true;
}

const std::string get_gpu_device_name(int device_id)
{
    cudaDeviceProp prop;
    std::string device_name = "";

    if (device_id != GPU_ID_INVALID) {
        cudaGetDeviceProperties(&prop, device_id);
        device_name = prop.name;
    }

    return device_name;
}

bool set_gpu_device(int gpu_id)
{
    cudaError_t cuda_err = cudaSuccess;

    cuda_err = cudaSetDevice(gpu_id);
    if (cuda_err != cudaSuccess) {
        std::cerr << "Failed to set gpu device." << std::endl;
        return false;
    }

    return true;
}

#ifndef TEGRA_ENABLED
/**
 * @brief Set GPU and memory clocks to locked on max frequency
 *
 * @param [in] gpu_id: GPU id
 *
 * @return: Return status of the operation.
 */
bool gpu_set_locked_clocks_max_freq(int gpu_id)
{
    nvmlReturn_t nvret = NVML_SUCCESS;
    bool ret = false;
    uint32_t max_graphics_clock_freq = 0;
    uint32_t max_memory_clock_freq = 0;

    nvret = nvmlInit();
    if (nvret != NVML_SUCCESS) {
        std::cerr << "Failed to init The NVIDIA Management Library (NVML) with error: "
            << nvret << std::endl;
        return false;
    }

    nvmlDevice_t nvDevice;
    nvret = nvmlDeviceGetHandleByIndex(gpu_id, &nvDevice);
    if (nvret != NVML_SUCCESS) {
        std::cerr << "Failed to get nvmlDevice with error: " << nvret << std::endl;
        goto end;
    }

    nvret = nvmlDeviceGetMaxClockInfo(nvDevice, NVML_CLOCK_GRAPHICS, &max_graphics_clock_freq);
    if (nvret != NVML_SUCCESS) {
        std::cerr << "Failed to get max graphics clock frequency with error: " << nvret << std::endl;
        goto end;
    }

    nvret = nvmlDeviceGetMaxClockInfo(nvDevice, NVML_CLOCK_MEM, &max_memory_clock_freq);
    if (nvret != NVML_SUCCESS) {
        std::cerr << "Failed to get max memory clock frequency with error: " << nvret << std::endl;
        goto end;
    }

    nvret = nvmlDeviceSetGpuLockedClocks(nvDevice, max_graphics_clock_freq, max_graphics_clock_freq);
    if (nvret != NVML_SUCCESS) {
        std::cerr << "Failed to set gpu clock on max frequency with error: " << nvret << std::endl;
        goto end;
    }

    nvret = nvmlDeviceSetMemoryLockedClocks(nvDevice, max_memory_clock_freq, max_memory_clock_freq);
    if (nvret != NVML_SUCCESS) {
        std::cerr << "Failed to set memory clock on max frequency with error: " << nvret << std::endl;
        goto end;
    }
    ret = true;

end:
    if (!ret) {
        gpu_reset_locked_clocks(gpu_id);
    }
    nvret = nvmlShutdown();
    if (nvret != NVML_SUCCESS) {
        std::cerr << "Failed to shutdown The NVIDIA Management Library (NVML) with error: "
            << nvret << std::endl;
        return false;
    }
    return ret;
}

/**
 * @brief Reset GPU and memory clocks to locked on default frequency
 *
 * @param [in] gpu_id: GPU id
 *
 * @return: Return status of the operation.
 */
bool gpu_reset_locked_clocks(int gpu_id)
{
    nvmlReturn_t nvret = NVML_SUCCESS;
    bool ret = false;

    nvret = nvmlInit();
    if (nvret != NVML_SUCCESS) {
        std::cerr << "Failed to init The NVIDIA Management Library (NVML) with error: "
            << nvret << std::endl;
        return false;
    }

    nvmlDevice_t nvDevice;
    nvret = nvmlDeviceGetHandleByIndex(gpu_id, &nvDevice);
    if (nvret != NVML_SUCCESS) {
        std::cerr << "Failed to get nvmlDevice with error: " << nvret << std::endl;
        goto end;
    }

    nvret = nvmlDeviceResetGpuLockedClocks(nvDevice);
    if (nvret != NVML_SUCCESS) {
        std::cerr << "Failed to reset gpu clock on default frequency with error: " << nvret << std::endl;
        goto end;
    }

    nvret = nvmlDeviceResetMemoryLockedClocks(nvDevice);
    if (nvret != NVML_SUCCESS) {
        std::cerr << "Failed to reset memory clock on default frequency with error: " << nvret << std::endl;
        goto end;
    }
    ret = true;

end:
    nvret = nvmlShutdown();
    if (nvret != NVML_SUCCESS) {
        std::cerr << "Failed to shutdown The NVIDIA Management Library (NVML) with error: "
            << nvret << std::endl;
        return false;
    }
    return ret;
}

/**
 * @brief: Query GPU BAR1 memory information
 *
 * @param [in] gpu_id   : GPU id.
 * @param [out] mem_info: GPU BAR1 memory information @ref gpu_bar1_memory_info
 *
 * @return: Return status of the operation.
 */
bool gpu_query_bar1_memory_info(int gpu_id, gpu_bar1_memory_info& mem_info)
{
    nvmlReturn_t nvret = NVML_SUCCESS;
    bool ret = false;

    nvret = nvmlInit();
    if (nvret != NVML_SUCCESS) {
        std::cerr << "Failed to init The NVIDIA Management Library (NVML) with error: "
            << nvret << std::endl;
        return false;
    }

    nvmlDevice_t nvDevice;
    nvret = nvmlDeviceGetHandleByIndex(gpu_id, &nvDevice);
    if (nvret != NVML_SUCCESS) {
        std::cerr << "Failed to get nvmlDevice with error: " << nvret << std::endl;
        goto end;
    }

    nvmlBAR1Memory_t nvBarMemory;
    nvret = nvmlDeviceGetBAR1MemoryInfo(nvDevice, &nvBarMemory);
    if (nvret != NVML_SUCCESS) {
        std::cerr << "Failed to get GPU BAR1 memory information with error: " << nvret << std::endl;
        goto end;
    }
    mem_info.free = nvBarMemory.bar1Free;
    mem_info.total = nvBarMemory.bar1Total;
    mem_info.used = nvBarMemory.bar1Used;
    ret = true;

end:
    nvret = nvmlShutdown();
    if (nvret != NVML_SUCCESS) {
        std::cerr << "Failed to shutdown The NVIDIA Management Library (NVML) with error: "
            << nvret << std::endl;
        return false;
    }
    return ret;
}

/**
 * @brief Verify that BAR1 has enough memory to allocate.
 *
 * @param [in] gpu_id: GPU id
 * @param [in] size  : size to compare with free size on BAR1
 *
 * @return: Return status of the operation.
 */
bool gpu_verify_allocated_bar1_size(int gpu_id, size_t size)
{
    gpu_bar1_memory_info bar1_mem_info;
    memset(&bar1_mem_info, 0, sizeof(bar1_mem_info));

    if (!gpu_query_bar1_memory_info(gpu_id, bar1_mem_info)) {
        std::cerr << "Failed to query GPU BAR1 memory information." << std::endl;
        return false;
    }

    if (size > bar1_mem_info.free) {
        std::cerr << "There is no enough BAR1 memory on the GPU; maximum size is " << bar1_mem_info.total
            << " MB. Unallocated free memory is " << bar1_mem_info.free << " MB (requested "
            << size << ")" << std::endl;
        return false;
    }
    return true;
}
#endif
#endif // CUDA_ENABLED

