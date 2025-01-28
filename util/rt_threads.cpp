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

#include <stdio.h>
#include <iostream>
#include <sstream>
#include <thread>
#include <string>
#include <cstring>
#include <chrono>
#include <functional>
#ifdef __linux__
#include <sys/epoll.h>
#include <unistd.h>
#include <signal.h>
#include <arpa/inet.h>
#else
#include <malloc.h>
#include <tchar.h>
#include <ws2tcpip.h>
#endif
#include <rivermax_api.h>
#include <rivermax_affinity.h>
#include "rt_threads.h"

using namespace std;

#if defined(_WIN32) || defined(_WIN64)
#include <windef.h>
#include <winbase.h>
#else
#include <pthread.h>
#endif

#define DEFAULT_CACHE_LINE_SIZE 64

using std::chrono::steady_clock;
using std::chrono::system_clock;
using std::chrono::milliseconds;
using std::chrono::microseconds;
using std::chrono::nanoseconds;
using std::chrono::seconds;
using std::chrono::duration_cast;
using std::chrono::duration;
using default_clock = system_clock;

bool cpu_affinity_get(stringstream &s, long &ret)
{
    char *endptr;
    long tmp;
    string nptr;

    getline(s, nptr, ',');
    if (nptr.size() == 0)
        return false;

    tmp = strtol(nptr.c_str(), &endptr, 10);
    if (*endptr) {
        cout << "Failed to convert affinity value to CPU index: " << nptr << endl;
        return false;
    }

    ret = tmp;
    return true;
}

bool rivermax_validate_thread_affinity_cpus(int internal_thread_affinity, std::vector<int> &cpus)
{
#if defined(_WIN32)
    DWORD_PTR process_affinity = 0;
    DWORD_PTR tmp = 0;

    if (!GetProcessAffinityMask(GetCurrentProcess(), &process_affinity, &tmp)) {
        cerr << "Failed obtaining process affinity mask returned: " << hex << process_affinity <<
            "Error:" << GetLastError() << endl;
        return false;
    }
    if ((internal_thread_affinity != CPU_NONE) &&
        !(((ULONG_PTR)1 << internal_thread_affinity) & process_affinity)) {
        cerr << "Requested thread affinity (" << internal_thread_affinity << ") "
                "is not in the process affinity (" << hex << process_affinity << ")" << endl;
        return false;
    }
    for (const auto cpu : cpus) {
        if (cpu == CPU_NONE)
            continue;
        if (!(((ULONG_PTR)1 << cpu) & process_affinity)) {
            cerr << "Requested thread affinity (" << cpu << ") "
                "is not in the process affinity (" << hex << process_affinity << ")" << endl;
            return false;
        }
    }
#else
    (void)(internal_thread_affinity);
    (void)(cpus);
#endif // defined(_WIN32)
    return true;
}

std::atomic_bool g_s_signal_received {false};

std::atomic_bool& exit_app()
{
    return g_s_signal_received;
}

static std::vector<std::function<void()>> g_signal_handler_cbs;

void register_signal_handler_cb(const std::function<void()>& callback)
{
    g_signal_handler_cbs.push_back(callback);
}

void initialize_signals()
{
    std::signal(SIGINT, signal_handler);
}

void signal_handler(const int signal_num)
{
    std::cout << "\n\n<--- Interrupt signal (" << signal_num << ") received --->\n\n\n";

    for (auto& callback : g_signal_handler_cbs) {
        callback();
    }
    g_s_signal_received = true;
}

#if defined(_WIN32) || defined(_WIN64)

HANDLE EventMgr::m_iocp = INVALID_HANDLE_VALUE;

int rt_set_thread_priority(int prio)
{
    if (!SetThreadPriority(GetCurrentThread(), prio)) {
        cerr << "Failed setting thread Priority: " << prio <<", Error: " << GetLastError() << endl;
        return 0;
    }
    return 1;
}

int rt_set_realtime_class(void)
{
    if (!SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS)) {
        cerr << "Failed SetPriorityClass, Error: " << GetLastError() << endl;
        return 0;
    }
    rt_set_thread_priority(-7);
    return 1;
}

struct font_color_win {
    HANDLE hConsole;
    WORD savedAttributes;
};

static WORD color_map[] = {
    FOREGROUND_RED | FOREGROUND_INTENSITY, /* COLOR_RED */
};

void *color_set(enum FONT_COLOR color)
{
    CONSOLE_SCREEN_BUFFER_INFO consoleInfo;
    font_color_win *fc = new font_color_win();

    if (!fc)
        return nullptr;

    fc->hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

    /* Save current attributes */
    GetConsoleScreenBufferInfo(fc->hConsole, &consoleInfo);
    fc->savedAttributes = consoleInfo.wAttributes;
    SetConsoleTextAttribute(fc->hConsole, color_map[color]);
    return fc;
}

void color_reset(void *ctx)
{
    font_color_win *fc = (font_color_win*)ctx;

    /* Restore original attributes */
    SetConsoleTextAttribute(fc->hConsole, fc->savedAttributes);
    delete fc;
}

typedef BOOL (WINAPI *LPFN_GLPI) (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION,
    PDWORD);

uint16_t get_cache_line_size(void)
{
    LPFN_GLPI glpi;
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION buffer = NULL;
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION ptr;
    DWORD return_length = 0;
    DWORD byte_offset = 0;
    PCACHE_DESCRIPTOR cache;
    DWORD rc;
    uint16_t line_sz = DEFAULT_CACHE_LINE_SIZE;

    glpi = (LPFN_GLPI)GetProcAddress(GetModuleHandle(TEXT("kernel32")),
        "GetLogicalProcessorInformation");
    if (!glpi) {
        cout << "GetLogicalProcessorInformation is not supported." << endl;
        goto exit;
    }

    rc = glpi(buffer, &return_length);
    if ((FALSE == rc) && (GetLastError() == ERROR_INSUFFICIENT_BUFFER)) {
        if (buffer) {
            free(buffer);
        }
        buffer = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION)malloc(
            return_length);
        if (!buffer) {
            cout << "Error: allocation failure" << endl;
            goto exit;
        }
        rc = glpi(buffer, &return_length);
    }
    if (FALSE == rc) {
        cout << "Error calling glpi(): " << GetLastError() << endl;
        goto exit;
    }

    ptr = buffer;
    while (byte_offset + sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION) <=
        return_length) {
        switch (ptr->Relationship) {
        case RelationCache:
            // Cache data is in ptr->Cache, one CACHE_DESCRIPTOR structure for each cache.
            cache = &ptr->Cache;
            if (cache->Level == 1) {
                line_sz = cache->LineSize;
                goto exit;
            }
            break;
        case RelationNumaNode:
        case RelationProcessorCore:
        case RelationProcessorPackage:
        default:
            break;
        }
        byte_offset += sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
        ptr++;
    }

exit:
    free(buffer);

    return line_sz;
}

uint16_t get_page_size(void)
{
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    return (uint16_t)sysInfo.dwPageSize;
}

#else // Linux

static const char *color_map[] = {
    [COLOR_RED] = "\x1B[31m",
    [COLOR_RESET] = "\x1B[0m"
};

void *color_set(enum FONT_COLOR color)
{
    std::cout << color_map[color];
    return nullptr;
}

void color_reset(void *ctx)
{
    (void)ctx;

    std::cout << color_map[COLOR_RESET];
}

int rt_set_thread_priority(int prio)
{
    (void)(prio);
    return 1;
}

int rt_set_realtime_class(void)
{
    return 1;
}

uint16_t get_cache_line_size(void)
{
    long size = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
    if (-1 == size) {
        cerr << "Warning - Failed retrieving cache line, using default " <<
                DEFAULT_CACHE_LINE_SIZE << endl;
        size = DEFAULT_CACHE_LINE_SIZE;
    }

    return static_cast<uint16_t>(size);
}

uint16_t get_page_size(void)
{
    uint16_t size = (uint16_t)sysconf(_SC_PAGESIZE);

    return size;
}
#endif // defined(_WIN32) || defined(_WIN64)

EventMgr::EventMgr()
#ifdef __linux
    : m_epoll_fd(-1)
#else
    : m_overlapped{}
#endif
{
    init_event_manager(INVALID_HANDLE_VALUE);
}

#ifdef __linux__
int EventMgr::init_event_manager(rmx_event_channel_handle event_channel_handle)
{
    if (-1 == m_epoll_fd) {
        int epoll_fd = epoll_create1(0);
        if (0 > epoll_fd) {
            std::cout << "Failed to create notification epoll file descriptor, errno: "
                << errno << std::endl;
            exit(-1);
        }
        m_epoll_fd = epoll_fd;
        m_event_channel_handle = event_channel_handle;
        return 0;
    }
    return -1;
}

int EventMgr::bind_event_channel(rmx_event_channel_handle event_channel_handle)
{
    if (INVALID_HANDLE_VALUE != m_epoll_fd) {
        struct epoll_event ev;
        memset(&ev, 0, sizeof(ev));
        ev.events = EPOLLIN | EPOLLOUT;
        ev.data.fd = event_channel_handle;
        if (epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, event_channel_handle, &ev)) {
            std::cout << "Failed to add fd " << event_channel_handle << " to epoll, errno: "
                << errno << std::endl;
            close(m_epoll_fd);
            exit(-1);
        }
        m_event_channel_handle = event_channel_handle;
        return 0;
    }
    return -1;
}
#else
int EventMgr::init_event_manager(rmx_event_channel_handle event_channel_handle)
{
    if (INVALID_HANDLE_VALUE == m_iocp) {
        // Create IOCP
        m_iocp = ::CreateIoCompletionPort(
            event_channel_handle,
            nullptr /*create new IOCP*/,
            0 /*completion key is ignored*/,
            0 /*concurrency is ignored with existing ports*/);
        if (!m_iocp) {
            std::cout << "Failed to create iocp with CreateIoCompletionPort for handle " << event_channel_handle << ", err=" << GetLastError() << std::endl;
            return -1;
        }
        m_event_channel_handle = event_channel_handle;
        return 0;
    }
    return 0;
}

int EventMgr::bind_event_channel(rmx_event_channel_handle event_channel_handle)
{
    if (INVALID_HANDLE_VALUE != m_iocp) {
        // Create IOCP
        m_iocp = ::CreateIoCompletionPort(
            event_channel_handle,
            m_iocp /*create new IOCP*/,
            0 /*completion key is ignored*/,
            0 /*concurrency is ignored with existing ports*/);
        if (!m_iocp) {
            std::cout << "Failed to bind event_channel " << event_channel_handle << " to IOCP " << m_iocp <<
                ", err=" << GetLastError() << std::endl;
            return -1;
        }
        m_event_channel_handle = event_channel_handle;
        return 0;
    }
    std::cout << "Failed to bind event_channel " << event_channel_handle << " to IOCP " << m_iocp <<
        ", err=" << GetLastError() << std::endl;
    return -1;
}
#endif

bool EventMgr::init(rmx_stream_id stream_id)
{
    rmx_status status;
    rmx_event_channel_params event_channel;
    rmx_event_channel_handle event_channel_handle;
    rmx_init_event_channel(&event_channel, stream_id);
    rmx_set_event_channel_handle(&event_channel, &event_channel_handle);

    status = rmx_establish_event_channel(&event_channel);
    if (status != RMX_OK) {
        std::cout << "Failed getting event channel with rmax_get_event_channel, status:" << status << std::endl;
        return false;
    }

    int ret = bind_event_channel(event_channel_handle);
    if (ret) {
        /* close event_channel */

        return false;
    }
    m_stream_id = stream_id;
    return true;
}

bool EventMgr::request_notification(rmx_stream_id stream_id)
{
    rmx_status ret;

    rmx_notification_params notification;
    rmx_init_notification(&notification, stream_id);
#ifndef __linux__
    rmx_set_notification_overlapped(&notification, &m_overlapped);
    m_stream_id = stream_id;
    if (m_request_completed) {
        // Some other stream has received the event and notified the current stream by setting the
        // m_request_completed for the current stream_id to true.
        // This means that some chunks have been sent and are now ready for re-use, so we may skip
        // the request for notification and go on to aquire the next chunk in the calling function.
        m_request_completed = false;
        return false;
    }
#endif
    ret = rmx_request_notification(&notification);

    if (ret == RMX_OK) {
        wait_for_notification(stream_id);
    } else if (ret != RMX_BUSY && ret != RMX_SIGNAL) {
        std::cerr << "Error returned by rmax_request_notification(): " << ret << std::endl;
        rmx_cleanup();
        exit(-1);
    }
    return true;
}

int EventMgr::wait_for_notification(rmx_stream_id stream_id)
{
#ifdef __linux
    stream_id = stream_id;
    if (-1 != m_epoll_fd) {
        struct epoll_event ev;
        memset(&ev, 0, sizeof(ev));
        ev.events = EPOLLIN | EPOLLOUT;
        if (0 > epoll_pwait(m_epoll_fd, &ev, 1, -1 /*wait forever*/, nullptr)) {
            if (EINTR != errno) {
                std::cout << "Failed to get an event with epoll_pwait, errno: "
                          << errno << std::endl;
                close(m_epoll_fd);
                exit(-1);
            }
        }
        return 0;
    }
    return -1;
#else
    if (m_iocp) {
        LPOVERLAPPED ovl;
        DWORD transferred = 0;
        ULONG_PTR completionKey = 0;
        BOOL ret = false;

        ret = ::GetQueuedCompletionStatus(m_iocp, &transferred, &completionKey, &ovl, INFINITE);
        EventMgr* ev_mgr = CONTAINING_RECORD(ovl, EventMgr, m_overlapped);
        if (ev_mgr->m_stream_id != stream_id) {
            // completion was for another stream, inform it
            ev_mgr->on_completion();
        }
        return !ret;
    }

    return -1;
#endif
}

EventMgr::~EventMgr()
{
#ifdef __linux__
    if (-1 != m_epoll_fd) {
        close(m_epoll_fd);
    }
#else
    if (m_iocp) {
        CloseHandle(m_iocp);
    }
#endif
}

#ifdef __linux__
int register_handler(int signum, void (*sig_handler)(int signum))
{
    struct sigaction action;

    action.sa_handler = sig_handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    sigaction(signum, &action, NULL);

    return 0;
}
#else
int register_handler(PHANDLER_ROUTINE sig_handler)
{
    if (!SetConsoleCtrlHandler(sig_handler, TRUE)) {
        std::cout << "Cannot set CTRL-C signal handler" << std::endl;
    }
    return 0;
}
#endif

void rt_set_thread_affinity(const std::vector<int>& cpu_core_affinities)
{
    bool needs_affinity = false;
    rivermax::libs::Affinity::mask cpu_affinity_mask;

    memset(&cpu_affinity_mask, 0, sizeof(cpu_affinity_mask));
    for (auto cpu : cpu_core_affinities) {
        if (cpu != CPU_NONE) {
            needs_affinity = true;
            RMAX_CPU_SET(cpu, &cpu_affinity_mask);
        }
    }

    if (needs_affinity) {
        rivermax::libs::set_affinity(cpu_affinity_mask);
    }
}

bool rt_set_rivermax_thread_affinity(int cpu_core)
{
    if (cpu_core == CPU_NONE) {
        return true;
    }
    if (cpu_core < 0) {
        std::cerr << "Invalid CPU core number " << cpu_core << std::endl;
        return false;
    }

    constexpr size_t cores_per_mask = 8 * sizeof(uint64_t);
    std::vector<uint64_t> cpu_mask(cpu_core / cores_per_mask + 1, 0);
    rmx_mark_cpu_for_affinity(cpu_mask.data(), cpu_core);
    rmx_status status = rmx_set_cpu_affinity(cpu_mask.data(), size_t(cpu_core) + 1);
    if (status != RMX_OK) {
        std::cerr << "Failed to set Rivermax CPU affinity to core " << cpu_core << ": " << status << std::endl;
        return false;
    }

    return true;
}

void rt_set_thread_affinity(const int cpu_core)
{
    if (cpu_core != CPU_NONE) {
        rivermax::libs::set_affinity(static_cast<size_t>(cpu_core));
    }
}

uint64_t default_time_handler(void*) /* XXX should be refactored and combined with media_sender's clock functions */
{
    return (uint64_t)duration_cast<nanoseconds>((default_clock::now() + seconds{ DEFAULT_LEAP_SECONDS }).time_since_epoch()).count();
}

double time_to_rtp_timestamp(double time_ns, int sample_rate)
{
    double time_sec = time_ns / static_cast<double>(std::chrono::nanoseconds{ std::chrono::seconds{1} }.count());
    double timestamp = time_sec * static_cast<double>(sample_rate);
    double mask = 0x100000000;
    // Decreasing RTP timestamp but not too much to have buffer both for delays
    // and advances in transmission or calculation imprecision
    timestamp -= 5;
    timestamp = std::fmod(timestamp, mask);
    return timestamp;
}

uint32_t convert_ip_str_to_int(const std::string& ipv4str)
{
    struct sockaddr_in sa;

    if (inet_pton(AF_INET, ipv4str.c_str(), &(sa.sin_addr)) != 1) {
        throw std::runtime_error("Invalied IP address: " + ipv4str);
    }

    return htonl(sa.sin_addr.s_addr);
}

bool assert_mc_ip(std::string ipv4str, std::string start_ipv4str, std::string end_ipv4str)
{
    uint32_t start  = convert_ip_str_to_int(start_ipv4str);
    uint32_t end = convert_ip_str_to_int(end_ipv4str);
    uint32_t ip = convert_ip_str_to_int(ipv4str);

    return !((ip & 0xF0000000) == 0xE0000000) || (start <= ip && ip <= end);
}

std::string get_local_time(uint64_t time_ns)
{
    char time_str[64];
    std::chrono::system_clock::time_point time_now{ std::chrono::duration_cast
       <std::chrono::system_clock::time_point::duration>(std::chrono::nanoseconds(time_ns)) };
    std::time_t time_format = std::chrono::system_clock::to_time_t(time_now);
    struct tm time_buff;
#ifdef __linux__
    localtime_r(&time_format, &time_buff);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-y2k"
    strftime(time_str, sizeof(time_str) - 1, "%c", &time_buff);
#pragma GCC diagnostic pop
#else
    localtime_s(&time_buff, &time_format);
    strftime(time_str, sizeof(time_str) - 1, "%c", &time_buff);
#endif
    return std::string(time_str);
}

int set_enviroment_variable(const std::string &name, const std::string &value)
{
#if defined(_WIN32) || defined(_WIN64)
    return _putenv_s(name.c_str(), value.c_str());
#else
    return setenv(name.c_str(), value.c_str(), 1);
#endif
}

rmx_status rivermax_setparam(const std::string &name, const std::string &value, bool forced)
{
    rmx_lib_param param;
    rmx_init_lib_param(&param);
    rmx_set_lib_param_name(&param, name.c_str());
    rmx_set_lib_param_value(&param, value.c_str());
    if (forced) {
        rmx_set_lib_param_forced(&param);
    }
    return rmx_apply_lib_param(&param);
}

bool rivermax_setparams(const std::vector<std::string> &assignments)
{
    for (auto &param_value_pair : assignments) {
        size_t pos = param_value_pair.find('=');
        if (pos == 0 || pos >= param_value_pair.length()) {
            std::cerr << "Invalid Rivermax parameter assignment string (" << param_value_pair << ")" << std::endl;
            return false;
        }
        std::string name = param_value_pair.substr(0, pos);
        std::string value = param_value_pair.substr(pos + 1);

        rmx_status status = rivermax_setparam(name, value, true);
        switch (status) {
            case RMX_OK:
                continue;
            case RMX_NOT_IMPLEMENTED:
                std::cerr << "Invalid Rivermax parameter name: " << name << std::endl;
                return false;
            case RMX_INVALID_PARAM_2:
                std::cerr << "Invalid Rivermax parameter value: " << value << std::endl;
                return false;
            default:
                std::cerr << "Assigning Rivermax parameter failed" << std::endl;
                return false;
        }
    }
    return true;
}

bool wait_rivermax_clock_steady()
{
    rmx_status status;
    while ((status = rmx_check_clock_steady()) == RMX_BUSY) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return (status == RMX_OK);
}
