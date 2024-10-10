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
#include <rivermax_api.h> // IWYU pragma: export
#include <thread>
#include <sys/sysinfo.h>

namespace rivermax {
namespace libs {

struct LinuxAffinity
{
    struct os_api {
        virtual ~os_api() = default;
        virtual int get_proc_count() const { return get_nprocs(); }
        virtual cpu_set_t *cpu_alloc(size_t count) const { return CPU_ALLOC(count); }
        virtual void cpu_free(cpu_set_t *cpu_set) const { CPU_FREE(cpu_set); }
        virtual size_t cpu_alloc_size(size_t count) const { return CPU_ALLOC_SIZE(count); }
        virtual void cpu_zero_s(size_t size, cpu_set_t *cpu_set) const { CPU_ZERO_S(size, cpu_set); }
        virtual void cpu_set(size_t processor, cpu_set_t *cpu_set) const { CPU_SET(processor, cpu_set); }
        virtual std::thread::native_handle_type this_thread_handle() const { return pthread_self(); }
        virtual int set_affinity_np (pthread_t handle, size_t set_size, const cpu_set_t *cpu_set) const
        { 
            auto status = pthread_setaffinity_np(handle, set_size, cpu_set); 
            return (status == 0)? 0: ((errno != 0) ? errno: -1);
        }
    };

    struct editor {
        editor(const LinuxAffinity &affinity, std::thread::native_handle_type thread);
        ~editor();
        void set(size_t processor);
        void apply();
    protected:
        const os_api &m_os_api;
        std::thread::native_handle_type m_thread;
        cpu_set_t *m_cpu_set;
        size_t m_set_size;
    };

    explicit LinuxAffinity(const os_api &os_api);
    size_t count_cores() const;

protected:
    const os_api &m_os_api;
};

struct OsSpecificAffinity : public LinuxAffinity 
{
    OsSpecificAffinity(const os_api &os_api): LinuxAffinity {os_api} {}
};

} // namespace libs
} // namespace rivermax
