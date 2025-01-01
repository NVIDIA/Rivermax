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

#include "rivermax_os_affinity.h"
#include <stdexcept>
#include <string>

namespace rivermax
{
namespace libs
{

WindowsAffinity::WindowsAffinity(const os_api &os_api) 
    : m_database{database::build(os_api)}, m_os_api {os_api} 
{
}

WindowsAffinity::database* WindowsAffinity::database::build(const os_api &win_api) 
{
    DWORD info_buffer_size = 0;
    auto status = win_api.get_logical_processor_information_ex(RelationGroup, nullptr, &info_buffer_size);
    if (status == ERROR_INSUFFICIENT_BUFFER) {
        database *groups = reinterpret_cast<database *>(operator new(info_buffer_size));
        status = win_api.get_logical_processor_information_ex(RelationGroup, groups, &info_buffer_size);
        if (status == NO_ERROR) {
            return groups;
        }
    }
    throw std::runtime_error("GetLogicalProcessorInformationEx returned error #" + std::to_string(status));
}

WindowsAffinity::editor::editor(const WindowsAffinity &affinity, std::thread::native_handle_type thread)
    : m_affinity {affinity}
    , m_thread {thread}
    , m_group {affinity.m_database->Group.GroupInfo}
    , m_mask {0} 
    , m_1st_processor_in_group {0}
{
}

void WindowsAffinity::editor::find_group(size_t processor) 
{
    const auto &database = m_affinity.m_database->Group;
    auto group_overflow = &database.GroupInfo[database.MaximumGroupCount];

    while (!is_in_current_group(processor)) {
        m_1st_processor_in_group += m_group->ActiveProcessorCount;
        if (++m_group == group_overflow) {
            throw std::runtime_error("Accessing non existing core #" + std::to_string(processor));
        }
    }
}

void WindowsAffinity::editor::determine_group(size_t processor) 
{
    if (m_mask != 0) {
        if (m_1st_processor_in_group > processor) {
            throw std::runtime_error("Affinity out of order for processor #" + std::to_string(processor));
        }
        else if (is_in_current_group(processor)) {
            return;
        }
        apply();
        m_mask = 0;
    }
    find_group(processor);
}

void WindowsAffinity::editor::set_ingroup_affinity(size_t processor) {
    size_t countdown = processor - m_1st_processor_in_group;
    size_t mask = 1;

    while ((0 == (m_group->ActiveProcessorMask & mask)) || (0 != countdown--)) {
        mask <<= 1;
    }

    m_mask |= mask;
}

void WindowsAffinity::editor::set(const size_t processor) {
    determine_group(processor);
    set_ingroup_affinity(processor);
}

void WindowsAffinity::editor::apply() 
{
    GROUP_AFFINITY affinity {m_mask, (WORD)(m_group - m_affinity.m_database->Group.GroupInfo), {0}};
    auto status = m_affinity.m_os_api.set_thread_group_affinity(m_thread, &affinity, nullptr);
    if (status != NO_ERROR) {
        throw std::runtime_error("SetThreadGroupAffinity returned error #" + std::to_string(status));
    }
}

size_t WindowsAffinity::count_cores() const {
    const auto begin = &m_database->Group.GroupInfo[0];
    auto end = &begin[m_database->Group.MaximumGroupCount];

    size_t count = 0;
    for (auto group = begin; group < end; ++group) {
        count += group->ActiveProcessorCount;
    }
    return count;
}

} // libs
} // rivermax
