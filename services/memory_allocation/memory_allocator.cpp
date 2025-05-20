/*
 * SPDX-FileCopyrightText: Copyright (c) 2022-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include <iostream>
#include <cstring>
#include <algorithm>
#ifdef __linux__
#include <sys/mman.h>
#include <stdlib.h>
#include <unistd.h>
#elif _WIN32
#include <windows.h>
#pragma comment(lib, "mincore")
#endif
#include "memory_allocator.h"
#include "gpu.h"

static constexpr int HUGE_PAGE_SIZE_VALUE_AUTO = 0;
static constexpr int HUGE_PAGE_SIZE_VALUE_2MB = 21;
static constexpr int HUGE_PAGE_SIZE_VALUE_512MB = 29;
static constexpr int HUGE_PAGE_SIZE_VALUE_1GB = 30;
static constexpr size_t HUGE_PAGE_SIZE_THRESHOLD = 2 * 1024 * 1024; // 2 MB
static constexpr int PAGE_SIZE_64KB = 65536;

void* MemoryAllocatorImp::allocate_new(const size_t length, size_t alignment)
{
    NOT_IN_USE(alignment);
    byte_t* mem_ptr = new (std::nothrow) byte_t[length];
    if (mem_ptr == nullptr) {
        std::cerr << "Failed to allocate " << length << " bytes" << std::endl;
        return nullptr;
    }

    return mem_ptr;
}

bool MemoryAllocatorImp::free_new(void* mem_ptr)
{
    if (mem_ptr == nullptr) {
        std::cerr << "Failed to free the pointer at address " << mem_ptr << std::endl;
        return false;
    }

    delete[] static_cast<byte_t*>(mem_ptr);
    mem_ptr = nullptr;

    return true;
}

std::shared_ptr<MemoryUtils> MemoryAllocatorImp::get_memory_utils_new()
{
    std::shared_ptr<MemoryUtils> utils_new(new NewMemoryUtils);

    return utils_new;
}

std::shared_ptr<MemoryUtils> MemoryAllocatorImp::get_memory_utils_malloc()
{
    std::shared_ptr<MemoryUtils> utils_malloc(new MallocMemoryUtils);

    return utils_malloc;
}

std::shared_ptr<MemoryUtils> MemoryAllocatorImp::get_memory_utils_huge_pages()
{
    std::shared_ptr<MemoryUtils> utils_huge_pages(new HugePagesMemoryUtils);

    return utils_huge_pages;
}

void* MemoryAllocatorImp::allocate_gpu(int gpu_id, size_t length, size_t alignment)
{
    return gpu_allocate_memory(gpu_id, length, alignment);
}

bool MemoryAllocatorImp::free_gpu(void* mem_ptr, size_t length)
{
    return gpu_free_memory(mem_ptr, length) ? true : false;
}

std::shared_ptr<MemoryUtils> MemoryAllocatorImp::get_memory_utils_gpu(int gpu_id)
{
    std::shared_ptr<MemoryUtils> utils_gpu(new GpuMemoryUtils(gpu_id));

    return utils_gpu;
}

int MemoryAllocatorImp::get_huge_page_size_log2() const
{
    int size_log2 = HUGE_PAGE_SIZE_VALUE_AUTO;
    const char *value = getenv("RIVERMAX_HUGE_PAGE_SIZE_LOG2");
    if (value) {
        if (strcmp(value, "auto") == 0) {
            size_log2 = HUGE_PAGE_SIZE_VALUE_AUTO;
        } else {
            size_log2 = std::max(0, atoi(value));
        }
    }

    switch (size_log2) {
    case HUGE_PAGE_SIZE_VALUE_AUTO:
        return get_default_huge_page_size_log2();
    default:
        return size_log2;
    }
}

size_t MemoryAllocatorImp::get_huge_page_size() const
{
    return size_t(1) << get_huge_page_size_log2();
}

#ifdef __linux__
void* LinuxMemoryAllocatorImp::allocate_malloc(const size_t length, size_t alignment)
{
    void* mem_ptr = aligned_alloc(alignment, length);
    if (mem_ptr == nullptr) {
        std::cerr << "Failed to allocate " << length << " bytes" << std::endl;
        return nullptr;
    }

    return mem_ptr;
}

bool LinuxMemoryAllocatorImp::free_malloc(void* mem_ptr)
{
    if (mem_ptr == nullptr) {
        std::cerr << "Failed to free the pointer at address " << mem_ptr << std::endl;
        return false;
    }

    free(mem_ptr);
    mem_ptr = nullptr;

    return true;
}

bool LinuxMemoryAllocatorImp::init_huge_pages(size_t& huge_page_size)
{
    m_huge_page_size_log2 = get_huge_page_size_log2();
    huge_page_size = get_huge_page_size();
    return true;
}

void* LinuxMemoryAllocatorImp::allocate_huge_pages(size_t length, size_t alignment)
{
    NOT_IN_USE(alignment);
    void* mem_ptr = mmap(nullptr, length, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_HUGETLB | (m_huge_page_size_log2 << MAP_HUGE_SHIFT), -1, 0);
    if (mem_ptr == MAP_FAILED) {
        std::cerr << "Failed to allocate " << length << " bytes using huge pages with errno " << errno << " (" << strerror(errno) << ")" << ", page size log2 = " << m_huge_page_size_log2 << std::endl;
        return nullptr;
    }
    return mem_ptr;
}

bool LinuxMemoryAllocatorImp::free_huge_pages(void* mem_ptr, size_t length)
{
    if (mem_ptr == nullptr) {
        std::cerr << "Failed to free the pointer at address " << mem_ptr << std::endl;
        return false;
    }
    if (munmap(mem_ptr, length)) {
        std::cerr << "Failed to free the pointer errno: " << errno << std::endl;
        return false;
    }
    return true;
}

int LinuxMemoryAllocatorImp::get_default_huge_page_size_log2() const
{
    switch (sysconf(_SC_PAGESIZE)) {
    case PAGE_SIZE_64KB:
        return HUGE_PAGE_SIZE_VALUE_512MB;
    default:
        return HUGE_PAGE_SIZE_VALUE_2MB;
    }
}

#elif _WIN32
WindowsMemoryAllocatorImp::WindowsMemoryAllocatorImp() :
    m_huge_page_extended_flag(MemExtendedParameterInvalidType)
{
}

void* WindowsMemoryAllocatorImp::allocate_malloc(const size_t length, size_t alignment)
{
    void* mem_ptr = _aligned_malloc(length, alignment);
    if (mem_ptr == nullptr) {
        std::cerr << "Failed to allocate " << length << " bytes" << std::endl;
        return nullptr;
    }

    return mem_ptr;
}

bool WindowsMemoryAllocatorImp::free_malloc(void* mem_ptr)
{
    if (mem_ptr == nullptr) {
        std::cerr << "Failed to free the pointer at address " << mem_ptr << std::endl;
        return false;
    }

    _aligned_free(mem_ptr);
    mem_ptr = nullptr;

    return true;
}

bool WindowsMemoryAllocatorImp::init_huge_pages(size_t& huge_page_size)
{
    HANDLE hToken;
    TOKEN_PRIVILEGES tp;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        std::cout << "Cannot use Large Pages, could not get privileges" << std::endl;
        return false;
    }

    // Used by local system to identify the privilege
    LUID luid;
    if (!LookupPrivilegeValue(NULL, TEXT("SeLockMemoryPrivilege"), &luid)) {
        std::cout << "Cannot use Large Pages, could not lookup privileges: SeLockMemoryPrivilege" << std::endl;
        CloseHandle(hToken);
        return false;
    }
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    tp.Privileges[0].Luid = luid;
    AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), NULL, NULL);
    DWORD last_error = GetLastError();
    if (last_error != ERROR_SUCCESS) {
        std::cout << "Cannot use Large Pages, failed setting privileges: error 0x" << std::hex << last_error << std::dec << std::endl;
        std::cout << "Following steps should be done due to enable Large Pages:\n"
            << "1. From the Start menu, open Local Security Policy (under Administrative Tools)\n"
            << "2. Under Local Policies\\User Rights Assignment, double click the Lock Pages in Memory setting\n"
            << "3. Click Add User or Group and type your Windows user name\n"
            << "4. Either log off and then log back in or restart your computer" << std::endl;
        CloseHandle(hToken);
        return false;
    }
    CloseHandle(hToken);

    huge_page_size = GetLargePageMinimum();
    if (huge_page_size == 0) {
        std::cout << "GetLargePageMinimum() error got zero 0x" << std::hex << GetLastError() << std::dec << std::endl;
        return false;
    } else {
        auto size_log2 = get_huge_page_size_log2();
        switch (size_log2) {
        case HUGE_PAGE_SIZE_VALUE_AUTO:
            m_huge_page_extended_flag = MemExtendedParameterInvalidType;
            break;
        case HUGE_PAGE_SIZE_VALUE_2MB:
            m_huge_page_extended_flag = MEM_EXTENDED_PARAMETER_NONPAGED_LARGE;
            huge_page_size = size_t(1) << size_log2;
            break;
        case HUGE_PAGE_SIZE_VALUE_1GB:
            m_huge_page_extended_flag = MEM_EXTENDED_PARAMETER_NONPAGED_HUGE;
            huge_page_size = size_t(1) << size_log2;
            break;
        default:
            std::cerr << "Unsupported huge page size log2 value " << size_log2 << ". Using system default " << huge_page_size << std::endl;
            m_huge_page_extended_flag = MemExtendedParameterInvalidType;
            break;
        }
    }
    return true;
}

void* WindowsMemoryAllocatorImp::allocate_huge_pages(size_t length, size_t alignment)
{
    NOT_IN_USE(alignment);
    ULONG num_ext_params = 0;
    MEM_EXTENDED_PARAMETER ext_param;
    MEM_EXTENDED_PARAMETER* ext_param_ptr = nullptr;
    if (m_huge_page_extended_flag != MemExtendedParameterInvalidType) {
        num_ext_params = 1;
        ext_param_ptr = &ext_param;
        std::memset(&ext_param, 0, sizeof(ext_param));
        ext_param.Type = MemExtendedParameterAttributeFlags;
        ext_param.ULong64 = m_huge_page_extended_flag;
    }
    void* mem_ptr = VirtualAlloc2(NULL, NULL, length, MEM_LARGE_PAGES | MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE, ext_param_ptr, num_ext_params);
    if (!mem_ptr) {
        auto last_error = GetLastError();
        std::cerr << "Failed to allocate " << length << " bytes using Large Pages with error: 0x" << std::hex << last_error << std::dec << std::endl;
        if (last_error == ERROR_INVALID_PARAMETER) {
            std::cerr << "Non default large page size is probably not supported" << std::endl;
            std::cerr << "Try setting environment variable RIVERMAX_HUGE_PAGE_SIZE_LOG2 to \"auto\"" << std::endl;
        }
        return nullptr;
    }

    std::cout << "Allocated " << length << " bytes using Large Pages" << std::endl;
    return mem_ptr;
}

bool WindowsMemoryAllocatorImp::free_huge_pages(void* mem_ptr, size_t length)
{
    NOT_IN_USE(length);
    if (mem_ptr == nullptr) {
        std::cerr << "Failed to free the pointer at address " << mem_ptr << std::endl;
        return false;
    }

    VirtualFree(mem_ptr, 0, MEM_RELEASE);
    mem_ptr = nullptr;
    return true;
}

int WindowsMemoryAllocatorImp::get_default_huge_page_size_log2() const
{
    return HUGE_PAGE_SIZE_VALUE_AUTO;
}
#endif

GpuMemoryUtils::GpuMemoryUtils(int gpu_id):
    m_gpu_id(gpu_id)
{
}

bool GpuMemoryUtils::init_thread() const
{
    // Must set gpu device on each new thread
    return set_gpu_device(m_gpu_id) ? true : false;
}

bool GpuMemoryUtils::memory_set(void* dst, int value, size_t count) const
{
    return gpu_memset(dst, value, count) ? true : false;
}

bool GpuMemoryUtils::memory_copy(void* dst, const void* src, size_t count) const
{
    return gpu_memcpy(dst, src, count)  ? true : false;
}

MemoryAllocator::MemoryAllocator()
    : m_imp(get_os_imp())
{
}

std::unique_ptr<MemoryAllocatorImp> MemoryAllocator::get_os_imp()
{
#ifdef __linux__
    return std::unique_ptr<MemoryAllocatorImp>(new LinuxMemoryAllocatorImp());
#elif _WIN32
    return std::unique_ptr<MemoryAllocatorImp>(new WindowsMemoryAllocatorImp());
#else
    throw "This OS is not yet implemented";
#endif
}

NewMemoryAllocator::~NewMemoryAllocator()
{
    bool rc;
    for (auto& mem_block : m_mem_blocks) {
        rc = m_imp->free_new(mem_block->pointer);
        if (rc == false) {
            std::cerr << "Failed to free memory using C++ delete[] operator" << std::endl;
        }
    }
}

void* NewMemoryAllocator::allocate(const size_t length, size_t alignment)
{
    size_t aligned_length = align_length(length, alignment);
    void* mem_ptr = m_imp->allocate_new(aligned_length, alignment);
    if (!mem_ptr) {
        std::cerr << "Failed to allocate memory using C++ new operator" << std::endl;
        return nullptr;
    }

    m_mem_blocks.push_back(std::unique_ptr<mem_block_t>(new mem_block_t{ mem_ptr, aligned_length }));

    uint64_t addr = reinterpret_cast<uint64_t>(mem_ptr);
    return reinterpret_cast<void*>((addr + alignment) & ~(alignment - 1));
}

bool NewMemoryAllocator::free()
{
    bool rc = false;
    for (auto& mem_block : m_mem_blocks) {
        rc = m_imp->free_new(mem_block->pointer);
        if (rc == false) {
            std::cerr << "Failed to free memory using C++ delete[] operator" << std::endl;
        }
    }

    return rc;
}

std::shared_ptr<MemoryUtils> NewMemoryAllocator::get_memory_utils()
{
    return m_imp->get_memory_utils_new();
}

size_t NewMemoryAllocator::align_length(size_t length, size_t alignment)
{
    return length + alignment;
}

MallocMemoryAllocator::~MallocMemoryAllocator()
{
    bool rc;
    for (auto& mem_block : m_mem_blocks) {
        rc = m_imp->free_malloc(mem_block->pointer);
        if (rc == false) {
            std::cerr << "Failed to free memory using free malloc" << std::endl;
        }
    }
}

void* MallocMemoryAllocator::allocate(const size_t length, size_t alignment)
{
    void* mem_ptr = m_imp->allocate_malloc(length, alignment);
    if (!mem_ptr) {
        std::cerr << "Failed to allocate memory using malloc/alloc" << std::endl;
        return nullptr;
    }

    m_mem_blocks.push_back(std::unique_ptr<mem_block_t>(new mem_block_t{ mem_ptr, length }));

    return mem_ptr;
}

bool MallocMemoryAllocator::free()
{
    bool rc = false;
    for (auto& mem_block : m_mem_blocks) {
        rc = m_imp->free_malloc(mem_block->pointer);
        if (rc == false) {
            std::cerr << "Failed to free memory using free malloc" << std::endl;
        }
    }

    return rc;
}

std::shared_ptr<MemoryUtils> MallocMemoryAllocator::get_memory_utils()
{
    return m_imp->get_memory_utils_malloc();
}

size_t MallocMemoryAllocator::align_length(size_t length, size_t alignment)
{
    NOT_IN_USE(alignment);
    return length;
}

HugePagesMemoryAllocator::HugePagesMemoryAllocator()
    : MemoryAllocator()
{
    if (!m_imp->init_huge_pages(m_huge_page_size)) {
        std::cerr << "Failed to initialize Huge Pages" << std::endl;
    } else {
        std::cout << "Initialized Huge Page allocator, page size = " << m_huge_page_size << std::endl;
    }
}

HugePagesMemoryAllocator::~HugePagesMemoryAllocator()
{
    bool rc;
    for (auto& mem_block : m_mem_blocks) {
        rc = m_imp->free_huge_pages(mem_block->pointer, mem_block->length);
        if (rc == false) {
            std::cerr << "Failed to free memory using Huge Pages" << std::endl;
        }
    }
}

void* HugePagesMemoryAllocator::allocate(const size_t length, size_t alignment)
{
    if (alignment == 1 && m_huge_page_size) {
        alignment = m_huge_page_size;
    }

    size_t aligned_length = align_length(length, alignment);
    void* mem_ptr = m_imp->allocate_huge_pages(aligned_length, alignment);
    if (!mem_ptr) {
        std::cerr << "Failed to allocate memory using Huge Pages" << std::endl;
        return nullptr;
    }

    m_mem_blocks.push_back(std::unique_ptr<mem_block_t>(new mem_block_t{ mem_ptr, aligned_length }));

    return mem_ptr;
}

bool HugePagesMemoryAllocator::free()
{
    bool rc = false;
    for (auto& mem_block : m_mem_blocks) {
        rc = m_imp->free_huge_pages(mem_block->pointer, mem_block->length);
        if (rc == false) {
            std::cerr << "Failed to free memory using Huge Pages" << std::endl;
        }
    }

    return rc;
}

std::shared_ptr<MemoryUtils> HugePagesMemoryAllocator::get_memory_utils()
{
    return m_imp->get_memory_utils_huge_pages();
}

size_t HugePagesMemoryAllocator::align_length(size_t length, size_t alignment)
{
    if (length < HUGE_PAGE_SIZE_THRESHOLD) {
        std::cout << "Note: Allocation using huge pages size requested " << length
            << " is smaller then one page size: " << alignment << std::endl;
    }
    size_t factor = length / alignment;
    factor += (length % alignment > 0) ? 1 : 0;
    return factor * alignment;
}

GpuMemoryAllocator::GpuMemoryAllocator(int gpu_id)
    : MemoryAllocator()
    , m_gpu_id(gpu_id)
{
}

GpuMemoryAllocator::~GpuMemoryAllocator()
{
    std::for_each(m_mem_blocks.begin()
                , m_mem_blocks.end()
                , [this](std::unique_ptr<mem_block_t>& mem_block){ m_imp->free_gpu(mem_block->pointer, mem_block->length); });
}

void* GpuMemoryAllocator::allocate(const size_t length, size_t alignment)
{
    if (alignment == 1) {
        alignment = 0;
    }
    size_t aligned_physical_memory = align_length(length, alignment);
    void* mem_ptr = m_imp->allocate_gpu(m_gpu_id, aligned_physical_memory, alignment);
    if (!mem_ptr) {
        std::cerr << "Failed to allocate memory on GPU id " << m_gpu_id << std::endl;
        return nullptr;
    }

    m_mem_blocks.push_back(std::unique_ptr<mem_block_t>(new mem_block_t{ mem_ptr, aligned_physical_memory }));

    return mem_ptr;
}

bool GpuMemoryAllocator::free()
{
    bool rc = false;
    for (auto& mem_block : m_mem_blocks) {
        rc = m_imp->free_gpu(mem_block->pointer, mem_block->length);
        if (rc == false) {
            std::cerr << "Failed to free GPU memory" << std::endl;
        }
    }

    return rc;
}

std::shared_ptr<MemoryUtils> GpuMemoryAllocator::get_memory_utils()
{
    return m_imp->get_memory_utils_gpu(m_gpu_id);
}

size_t GpuMemoryAllocator::align_length(size_t length, size_t alignment)
{
    // Align physical size on GPU to the device granularity.
    NOT_IN_USE(alignment);
    return gpu_align_physical_allocation_size(m_gpu_id, length);
}
