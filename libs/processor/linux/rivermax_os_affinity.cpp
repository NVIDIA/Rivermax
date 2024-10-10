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

#include "rivermax_os_affinity.h"
#include <stdexcept>
#include <string>

namespace rivermax
{
namespace libs
{

LinuxAffinity::LinuxAffinity(const os_api &os_api)
    : m_os_api {os_api} 
{
}

LinuxAffinity::editor::editor(const LinuxAffinity &affinity, std::thread::native_handle_type thread) 
    : m_os_api {affinity.m_os_api}, m_thread {thread}
{
    m_cpu_set = m_os_api.cpu_alloc(RMAX_CPU_SETSIZE);
    if (m_cpu_set == nullptr) {
        throw std::runtime_error("failed to allocate cpu_set for " + std::to_string(RMAX_CPU_SETSIZE) + " cpus");
    }
    m_set_size = m_os_api.cpu_alloc_size(RMAX_CPU_SETSIZE);
    m_os_api.cpu_zero_s(m_set_size, m_cpu_set);
}

void LinuxAffinity::editor::set(size_t processor) 
{
    if (processor >= RMAX_CPU_SETSIZE) {
        throw std::runtime_error("failed to apply illegal core number: " + std::to_string(processor) );
    }    
    m_os_api.cpu_set(processor, m_cpu_set);
}

void LinuxAffinity::editor::apply() 
{
    auto status = m_os_api.set_affinity_np(m_thread, m_set_size, m_cpu_set);
    if (status != 0) {
        throw std::runtime_error("failed setting thread affinity, errno: " + std::to_string(status));
    }
}

LinuxAffinity::editor::~editor() 
{
    m_os_api.cpu_free(m_cpu_set);
}

size_t LinuxAffinity::count_cores() const 
{ 
    return m_os_api.get_proc_count(); 
}

} // namespace libs
} // namespace rivermax
