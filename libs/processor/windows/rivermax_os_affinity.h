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

#pragma once
#include <cstddef>
#include <memory>
#include <thread>
#include <rivermax_api.h> // IWYU pragma: export

namespace rivermax
{
namespace libs
{

struct WindowsAffinity
{
  struct os_api
  {
    virtual DWORD get_logical_processor_information_ex(const LOGICAL_PROCESSOR_RELATIONSHIP RelationshipType,
      SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *Buffer, DWORD *ReturnedLength) const
    {
      return to_error_code(GetLogicalProcessorInformationEx(RelationshipType, Buffer, ReturnedLength));
    }

    virtual DWORD get_numa_processor_node_ex(PROCESSOR_NUMBER *Processor, USHORT *NodeNumber) const
    {
      return to_error_code(GetNumaProcessorNodeEx(Processor, NodeNumber));
    }

    virtual DWORD set_thread_group_affinity(HANDLE hThread, const GROUP_AFFINITY *GroupAffinity, 
      GROUP_AFFINITY *PreviousGroupAffinity) const
    {
      return to_error_code(SetThreadGroupAffinity(hThread, GroupAffinity, PreviousGroupAffinity));
    }
    virtual std::thread::native_handle_type this_thread_handle() const { return ::GetCurrentThread(); }

  protected:
    DWORD to_error_code(BOOL return_value) const
    {
      if (return_value) return 0;
      auto error_code = GetLastError();
      return error_code ? error_code : (DWORD)(-1);
    }
  };


  struct editor {
    editor(const WindowsAffinity &affinity, std::thread::native_handle_type thread);
    void set(const size_t processor);
    void apply();

  private:
    const WindowsAffinity &m_affinity;
    std::thread::native_handle_type m_thread;

    const PROCESSOR_GROUP_INFO *m_group;
    KAFFINITY m_mask;
    size_t m_1st_processor_in_group;

    void find_group(size_t processor);
    void determine_group(size_t processor);
    void set_ingroup_affinity(size_t processor);

    bool is_in_current_group(size_t processor) {
      return ((m_1st_processor_in_group + m_group->ActiveProcessorCount) > processor);
    }
  };

  struct database: SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX {
    static database* build(const os_api &win_api);
  };

  WindowsAffinity(const os_api &os_api);
  size_t count_cores() const;

protected:
  std::unique_ptr<database> m_database;
  const os_api &m_os_api;
};

struct OsSpecificAffinity : public WindowsAffinity
{
  OsSpecificAffinity(const os_api &os_api) : WindowsAffinity {os_api} {}
};

} // libs
} // rivermax
