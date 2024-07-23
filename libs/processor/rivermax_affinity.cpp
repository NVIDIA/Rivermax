/*
 * Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. ALL RIGHTS RESERVED.
 *
 * This software product is a proprietary product of Nvidia Corporation and its affiliates
 * (the "Company") and all right, title, and interest in and to the software
 * product, including all associated intellectual property rights, are and
 * shall remain exclusively with the Company.
 *
 * This software product is governed by the End User License Agreement
 * provided with the software product.
 */

#include "rivermax_affinity.h"
#include <cstddef>
#include <cstdio>
#include <exception>
#include <stdexcept>

namespace rivermax
{
namespace libs
{

const Affinity::os_api Affinity::default_api;

Affinity::Affinity(const os_api &os_api)
    : OsSpecificAffinity {os_api}
{
}

Affinity::~Affinity()
{
}

void Affinity::set(std::thread &thread, const size_t processor)
{
    editor editor(*this, thread.native_handle());
    editor.set(processor);
    editor.apply();
}

void Affinity::set(std::thread &thread, const mask &cpu_mask)
{
    editor editor(*this, thread.native_handle());
    fill_with(cpu_mask, editor);    
    editor.apply();
}

void Affinity::set(const size_t processor)
{
    editor editor(*this, m_os_api.this_thread_handle());
    editor.set(processor);
    editor.apply();
}

void Affinity::set(const mask &cpu_mask)
{
    editor editor(*this, m_os_api.this_thread_handle());
    fill_with(cpu_mask, editor);
    editor.apply();
}

void Affinity::fill_with(const mask &cpu_mask, editor &editor)
{
    size_t processor = 0;
    size_t count = 0;
    for (auto entry: cpu_mask.rmax_bits) {
        if (!entry) {
            processor += sizeof(rmax_cpu_mask_t) * 8;
            continue;
        }
        for (rmax_cpu_mask_t mask = 1; mask; mask <<= 1, processor++) {
            if (entry & mask) {
                editor.set(processor);
                ++count;
            }
        }
    }
    if (count == 0) {
        throw std::underflow_error("Affinity mask shall not be all-zeros.");
    }
}

bool set_affinity(const size_t processor) noexcept 
{
    try {
        Affinity().set(processor);
    }
    catch (const std::exception& e) {
        printf("failed to set a core affinity: %s\n", e.what());
        return false;
    }
    return true;
}

bool set_affinity(const Affinity::mask &cpu_mask) noexcept
{
    try {
        Affinity().set(cpu_mask);
    }
    catch (const std::exception& e) {
        printf("failed to set cpu core affinities: %s\n", e.what());
        return false;
    }
    return true;  
}

} // namespace libs
} // namespace rivermax
