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
