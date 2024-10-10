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
