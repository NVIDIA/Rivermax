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

#ifndef MEMORY_ALLOCATOR_H
#define MEMORY_ALLOCATOR_H

#include <vector>
#include <memory>
#include <mutex>

typedef uint8_t byte_t;

/**
 * @brief: Memory block representation.
 *
 * The struct represents memory block.
 *
 * @param [in] pointer: Pointer to the start address of the memory.
 * @param [in] length: Memory block length.
 */
typedef struct mem_block
{
    void* pointer;
    size_t length;
} mem_block_t;

 /**
 * @brief: Memory utils interface.
 *
 * The memory utils interface to implement when adding new type of @MemoryAllocator
 * that need new memory utilities to handle the new memory type.
 * Implementors of this interface, should add implementation of compatible method
 * in MemoryAllocatorImp class, this function will export the memory utilities
 * relevant to the specific memory Allocator.
 */
class MemoryUtils
{
public:
    virtual ~MemoryUtils() = default;
    /**
     * @brief: Initialize memory utils on thread.
     *
     * @return: Return true in success, false otherwise.
     */
    virtual bool init_thread() const = 0;
    /**
     * @brief: Set memory.
     *
     * @param [in] dst: Destination memory address.
     * @param [in] value: Value to set for each byte of specified memory.
     * @param [in] count: Size in bytes to set.
     *
     * @return: Return true in success, false otherwise.
     */
    virtual bool memory_set(void* dst, int value, size_t count) const = 0;
    /**
     * @brief: Copy memory.
     *
     * @param [in] dst: Destination memory address.
     * @param [in] src:  Source memory address.
     * @param [in] count: Size in bytes to copy.
     *
     * @return: Return true in success, false otherwise.
     */
    virtual bool memory_copy(void* dst, const void* src, size_t count) const = 0;
};

/**
 * @brief: C++ new operator memory utilities.
 *
 * Implements @ref MemoryUtils interface.
 */
class NewMemoryUtils : public MemoryUtils
{
public:
    inline bool init_thread() const override
    {
        return true;
    };
    inline bool memory_set(void* dst, int value, size_t count) const override
    {
        memset(dst, value, count);
        return true;
    }
    inline bool memory_copy(void* dst, const void* src, size_t count) const override
    {
        memcpy(dst, src, count);
        return true;
    };
};

/**
* @brief: Malloc memory utilities.
*
* Implements @ref MemoryUtils interface.
*/
class MallocMemoryUtils : public MemoryUtils
{
public:
    inline bool init_thread() const override
    {
        return true;
    };
    inline bool memory_set(void* dst, int value, size_t count) const override
    {
        memset(dst, value, count);
        return true;
    }
    inline bool memory_copy(void* dst, const void* src, size_t count) const override
    {
        memcpy(dst, src, count);
        return true;
    }
};

/**
* @brief: Huge Pages memory utilities
*
* Implements @ref MemoryUtils interface
*/
class HugePagesMemoryUtils : public MemoryUtils
{
public:
    inline bool init_thread() const override
    {
        return true;
    }
    inline bool memory_set(void* dst, int value, size_t count) const override
    {
        memset(dst, value, count);
        return true;
    }
    inline bool memory_copy(void* dst, const void* src, size_t count) const override
    {
        memcpy(dst, src, count);
        return true;
    }
};

/**
 * @brief: GPU memory utilities.
 *
 * Implements @ref MemoryUtils interface.
 */
class GpuMemoryUtils : public MemoryUtils
{
private:
    const int m_gpu_id = -1;
public:
    explicit GpuMemoryUtils(int gpu_id);
    bool init_thread() const override;
    bool memory_set(void* dst, int value, size_t count) const override;
    bool memory_copy(void* dst, const void* src, size_t count) const override;
};

/**
 * @brief: Memory allocator implementation.
 *
 * The base class for cross platform memory allocation and deallocation.
 * This class implements the bridge design pattern, decoupling the
 * abstraction from the implementation.
 * All future memory allocation methods, should be declared here as
 * virtual methods and implemented in the derived OS specific classes.
 */
class MemoryAllocatorImp
{
public:
    virtual ~MemoryAllocatorImp() = default;
    /**
     * @brief: Allocates memory using C++ new operator.
     *
     * @param [in] length   : Length of the memory to allocate.
	 * @param [in] alignment: Aligment size of the memory to allocate.
	 *
     * @return: Pointer to the allocated memory.
     */
    virtual void* allocate_new(const size_t length, size_t alignment);
    /**
     * @brief: Frees memory using C++ delete operator.
     *
     * @param [in] mem_ptr: Pointer to the memory to free.
     *
     * @return: Return true in success, false otherwise.
     */
    virtual bool free_new(void* mem_ptr);
    /**
     * @brief: Return new memory utils.
     *
     * @return: Shared pointer to the memory utils.
     */
    virtual std::shared_ptr<MemoryUtils> get_memory_utils_new();
    /**
     * @brief: Allocates memory using aligned_alloc/_aligned_malloc.
     *
     * @param [in] length   : Length of the memory to allocate.
     * @param [in] alignment: Aligment size of the memory to allocate.
     *
     * @return: Pointer to the allocated memory.
     */
    virtual void* allocate_malloc(const size_t length, size_t alignment) = 0;
    /**
     * @brief: Frees memory using free free/_aligned_free.
     *
     * @param [in] mem_ptr: Pointer to the memory to free.
     *
     * @return: Return true in success, false otherwise.
     */
    virtual bool free_malloc(void* mem_ptr) = 0;
    /**
     * @brief: Return malloc memory utils.
     *
     * @return: Shared pointer to the memory utils.
     */
    virtual std::shared_ptr<MemoryUtils> get_memory_utils_malloc();
    /**
     * @brief: Initialize huge pages.
     *
     * @param [out] huge_page_size: Supported Huge Page size.
     *
     * @return: Return true in success, false otherwise.
     */
    virtual bool init_huge_pages(size_t& huge_page_size) = 0;
    /**
     * @brief: Allocates memory using Huge Pages alocation.
     *
     * @param [in] length   : Length of the memory to allocate.
	 * @param [in] alignment: Aligment size of the memory to allocate.
	 *
     * @return: Pointer to the allocated memory.
     */
    virtual void* allocate_huge_pages(size_t length, size_t alignment) = 0;
    /**
     * @brief: Frees memory using Huge Pages delete operator.
     *
     * @param [in] mem_ptr: Pointer to the memory to free.
     * @param [in] length : Length of the memory to free.
     *
     * @return: Return true in success, false otherwise.
     */
    virtual bool free_huge_pages(void* mem_ptr, size_t length) = 0;
    /**
     * @brief: Return Huge Pages memory utils.
	 *
     * @return: Shared pointer to the memory utils.
     */
    virtual std::shared_ptr<MemoryUtils> get_memory_utils_huge_pages();
    /**
     * @brief: Allocates memory using cuda.
     *
     * @param [in] gpu_id   : GPU id.
	 * @param [in] length   : Length of the memory to allocate.
	 * @param [in] alignment: Alignment of the reserved virtual address length requested memory to allocate.
     *
     * @return: Pointer to the allocated memory.
     */
    virtual void* allocate_gpu(int gpu_id, size_t length, size_t alignment);
    /**
     * @brief: Frees memory using cuda.
     *
     * @param [in] mem_ptr: Pointer to the memory to free.
     * @param [in] length : Length of the memory to free.
     *
     * @return: Return true in success, false otherwise.
     */
    virtual bool free_gpu(void* mem_ptr, size_t length);
    /**
     * @brief: Return GPU memory utils.
     *
     * @param [in] gpu_id: Gpu device id.
     *
     * @return: Shared pointer to the memory utils.
     */
     virtual std::shared_ptr<MemoryUtils> get_memory_utils_gpu(int gpu_id);
protected:
    /**
     * @brief: Returns the default value for the huge page size suitable for
     * the current OS kernel.
     *
     * @return: Huge page size (log2), always positive.
     */
    virtual int get_default_huge_page_size_log2() const = 0;
    /**
     * @brief: Read huge page size parameter from environment variable.
     *
     * @description Supported values are 0 or "auto" for page size
     * auto-selection or a positive number specifying a log2 of huge page size.
     *
     * @return: Huge page size (log2), always positive.
     */
    int get_huge_page_size_log2() const;
    /**
     * @brief: Return huge page size in bytes.
     *
     * @return: Page size in bytes.
     */
    size_t get_huge_page_size() const;
};

/**
 * @brief: Linux memory allocator implementation.
 *
 * Implements @ref MemoryAllocatorImp interface for Linux specific memory allocation.
 */
class LinuxMemoryAllocatorImp : public MemoryAllocatorImp
{
private:
    int m_huge_page_size_log2 = 0;
public:
    virtual void* allocate_malloc(const size_t length, size_t alignment) override;
    virtual bool free_malloc(void* mem_ptr) override;
    virtual bool init_huge_pages(size_t& huge_page_size) override;
    virtual void* allocate_huge_pages(size_t length, size_t alignment) override;
    virtual bool free_huge_pages(void* mem_ptr, size_t length) override;
    virtual int get_default_huge_page_size_log2() const override;
};

/**
 * @brief: Windows memory allocator implementation.
 *
 * Implements @ref MemoryAllocatorImp interface for Windows specific memory allocation.
 */
class WindowsMemoryAllocatorImp : public MemoryAllocatorImp
{
private:
    uint64_t m_huge_page_extended_flag;
public:
    WindowsMemoryAllocatorImp();
    virtual void* allocate_malloc(const size_t length, size_t alignment) override;
    virtual bool free_malloc(void* mem_ptr) override;
    virtual bool init_huge_pages(size_t& huge_page_size) override;
    virtual void* allocate_huge_pages(size_t length, size_t alignment) override;
    virtual bool free_huge_pages(void* mem_ptr, size_t length) override;
    virtual int get_default_huge_page_size_log2() const override;
};

/**
 * @brief: Memory allocator manager interface.
 *
 * The memory allocator interface to implement when adding new type of
 * memory allocations.
 * Implementors of this interface, should add the memory implementation
 * specific logic, by expanding @ref MemoryAllocatorImp interface with
 * a compatible methods for allocation and free of the new added memory allocation type.
 */
class MemoryAllocator
{
protected:
    /* OS specific memory allocator implementation */
    std::unique_ptr<MemoryAllocatorImp> m_imp;
    /* Container for the memory blocks allocated */
    std::vector<std::unique_ptr<mem_block_t>> m_mem_blocks;
    /**
     * @brief: Align length for memory allocation.
     *
     * @param [in] length   : Requested allocation size.
     * @param [in] alignment: Aligment size of the memory to allocate.
     *
     * @return: Return aligned allocation size.
     */
    virtual size_t align_length(size_t length, size_t alignment) = 0;
public:
    MemoryAllocator();
    virtual ~MemoryAllocator() = default;
    /**
     * @brief: Allocates memory.
     *
     * The method to implement for @ref MemoryAllocator interface.
     * Implementors of this interface should delegate the implementation
     * to the compatible method in @ref MemoryAllocatorImp implementation.
     *
	 * @param [in] length   : Length of the memory to allocate.
	 * @param [in] alignment: Aligment size of the memory to allocate.
     *
     * @return: Pointer to the allocated memory.
     */
    virtual void* allocate(const size_t length, size_t alignment = 1) = 0;
    /**
     * @brief: Free memory.
     *
     * The method to implement for @ref MemoryAllocator interface.
     * Implementors of this interface should delegate the implementation
     * to the compatible method in @ref MemoryAllocatorImp implementation.
     *
     * @return: Return true in success, false otherwise.
     */
    virtual bool free() = 0;
    /**
     * @brief: Return memory utils.
     *
     * The method to implement for @ref MemoryAllocator interface.
     * Implementors of this interface should delegate the implementation
     * to the compatible method in @ref MemoryAllocatorImp implementation.
     *
     * @return: Shared pointer to the memory utils.
     */
    virtual std::shared_ptr<MemoryUtils> get_memory_utils() = 0;

private:
    /**
     * @brief: Returns OS MemoryAllocatorImp.
     *
     * Factory method to create the OS specific memory allocator implementation object.
     *
     * @return: Returns OS MemoryAllocatorImp unique pointer.
     */
    std::unique_ptr<MemoryAllocatorImp> get_os_imp();
};

/**
 * @brief: C++ new operator memory allocation.
 *
 * Implements @ref MemoryAllocator interface for allocating memory using C++ new operator.
 */
class NewMemoryAllocator : public MemoryAllocator
{
public:
    ~NewMemoryAllocator();
    void* allocate(const size_t length, size_t alignment) override;
    bool free() override;
    std::shared_ptr<MemoryUtils> get_memory_utils() override;
private:
    size_t align_length(size_t length, size_t alignment) override;
};

/**
* @brief: Malloc memory allocation.
*
* Implements @ref MemoryAllocator interface for allocating memory using aligned_alloc/_aligned_malloc.
*/
class MallocMemoryAllocator : public MemoryAllocator
{
public:
    ~MallocMemoryAllocator();
    void* allocate(const size_t length, size_t alignment) override;
    bool free() override;
    std::shared_ptr<MemoryUtils> get_memory_utils() override;
private:
    size_t align_length(size_t length, size_t alignment) override;
};

/**
 * @brief: Huge Pages memory allocation.
 *
 * Implements @ref MemoryAllocator interface for allocating memory using Huge Pages.
 */
class HugePagesMemoryAllocator : public MemoryAllocator
{
public:
    explicit HugePagesMemoryAllocator();
    ~HugePagesMemoryAllocator();
    void* allocate(const size_t length, size_t alignment) override;
    bool free() override;
    std::shared_ptr<MemoryUtils> get_memory_utils() override;
private:
    size_t m_huge_page_size;
    size_t align_length(size_t length, size_t alignment) override;
};

/**
 * @brief: GPU memory allocation.
 *
 * Implements @ref MemoryAllocator interface for allocating memory using GPU device.
 */
class GpuMemoryAllocator : public MemoryAllocator
{
public:
    explicit GpuMemoryAllocator(int gpu_id);
    ~GpuMemoryAllocator();
    void* allocate(const size_t length, size_t alignment) override;
    bool free() override;
    std::shared_ptr<MemoryUtils> get_memory_utils() override;
private:
    int m_gpu_id = -1;
    size_t align_length(size_t length, size_t alignment) override;
};

#endif /* MEMORY_ALLOCATOR_H */
