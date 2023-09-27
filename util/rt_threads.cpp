/*
 * SPDX-FileCopyrightText: Copyright (c) 2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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
#include "rivermax_api.h"
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

std::atomic_bool& exit_app()
{
    return g_s_signal_received;
}

void initialize_signals()
{
    std::signal(SIGINT, signal_handler);
}

void signal_handler(const int signal_num)
{
    std::cout << "\n\n<--- Interrupt signal (" << signal_num << ") received --->\n\n\n";
    g_s_signal_received = true;
}

#if defined(_WIN32) || defined(_WIN64)

HANDLE EventMgr::m_iocp = INVALID_HANDLE_VALUE;

bool rt_set_thread_affinity(struct rmax_cpu_set_t *cpu_mask)
{
    DWORD_PTR ret;

    for (int iter = 1; iter < RMAX_CPU_SETSIZE / RMAX_NCPUBITS; iter++) {
        if (cpu_mask->rmax_bits[iter]) {
            cerr << "Rivermax doesn't support setting thread affinity to CPUs beyond the first 64" << endl;
        }
    }

    DWORD_PTR thread_affinity = static_cast<DWORD_PTR>(cpu_mask->rmax_bits[0]);
    if (!thread_affinity)
        return true;

    HANDLE thread_handle = GetCurrentThread();
    ret = SetThreadAffinityMask(thread_handle, thread_affinity);
    if (!ret) {
        cerr << "Failed setting CPU affinity, Error " << GetLastError() << endl;
        return false;
    } else {
        cout << "Successfully set thread affinity using cpu mask: 0x" << hex << thread_affinity << ", previous mask: 0x" << hex << ret << dec << endl;
    }

    return true;
}

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

bool rt_set_thread_affinity(struct rmax_cpu_set_t *cpu_mask)
{
    /* set thread affinity */
    int ret = 0;
    cpu_set_t *cpu_set;
    rmax_cpu_mask_t major;
    size_t cpu_alloc_size;

    cpu_set = CPU_ALLOC(RMAX_CPU_SETSIZE);
    if (!cpu_set) {
        cerr << "failed to allocate cpu_set for " << RMAX_CPU_SETSIZE << " CPUs" << endl;
        return false;
    }
    cpu_alloc_size = CPU_ALLOC_SIZE(RMAX_CPU_SETSIZE);
    CPU_ZERO_S(cpu_alloc_size, cpu_set);

    /* Run through all rmax_cpu_mask_t blocks and add any set bit to cpu_set */
    for (major = 0; major < RMAX_CPU_SETSIZE / RMAX_NCPUBITS; major++) {
        rmax_cpu_mask_t current;
        rmax_cpu_mask_t minor;

        for (current = cpu_mask->rmax_bits[major], minor = 0; current; current >>= 1, minor++) {
            if (current & 0x1) {
                CPU_SET(major * RMAX_NCPUBITS + minor, cpu_set);
            }
        }
    }

    pthread_t thread_handle = pthread_self();

    if (CPU_COUNT(cpu_set)) {
        ret = pthread_setaffinity_np(thread_handle, cpu_alloc_size, cpu_set);
        if (ret) {
            cerr << "failed setting thread affinity, errno: " << errno << endl;
        } else {
            cout << "successfully set thread affinity using cpu set: 0x" << hex << cpu_set->__bits[0] << dec << endl;
        }
    }

    CPU_FREE(cpu_set);

    return ret ? false: true;
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
    uint16_t size = (uint16_t)sysconf(_SC_LEVEL1_DCACHE_LINESIZE);

    return size;
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
int EventMgr::init_event_manager(rmax_event_channel_t event_channel)
{
    if (-1 == m_epoll_fd) {
        int epoll_fd = epoll_create1(0);
        if (0 > epoll_fd) {
            std::cout << "Failed to create notification epoll file descriptor, errno: "
                << errno << std::endl;
            exit(-1);
        }
        m_epoll_fd = epoll_fd;
        m_event_channel = event_channel;
        return 0;
    }
    return -1;
}

int EventMgr::bind_event_channel(rmax_event_channel_t event_channel)
{
    if (INVALID_HANDLE_VALUE != m_epoll_fd) {
        struct epoll_event ev;
        memset(&ev, 0, sizeof(ev));
        ev.events = EPOLLIN | EPOLLOUT;
        ev.data.fd = event_channel;
        if (epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, event_channel, &ev)) {
            std::cout << "Failed to add fd " << event_channel << " to epoll, errno: "
                << errno << std::endl;
            close(m_epoll_fd);
            exit(-1);
        }
        m_event_channel = event_channel;
        return 0;
    }
    return -1;
}
#else
int EventMgr::init_event_manager(rmax_event_channel_t event_channel)
{
    if (INVALID_HANDLE_VALUE == m_iocp) {
        // Create IOCP
        m_iocp = ::CreateIoCompletionPort(
            event_channel,
            nullptr /*create new IOCP*/,
            0 /*completion key is ignored*/,
            0 /*concurrency is ignored with existing ports*/);
        if (!m_iocp) {
            std::cout << "Failed to create iocp with CreateIoCompletionPort for handle " << event_channel << ", err=" << GetLastError() << std::endl;
            return -1;
        }
        m_event_channel = event_channel;
        return 0;
    }
    return 0;
}

int EventMgr::bind_event_channel(rmax_event_channel_t event_channel)
{
    if (INVALID_HANDLE_VALUE != m_iocp) {
        // Create IOCP
        m_iocp = ::CreateIoCompletionPort(
            event_channel,
            m_iocp /*create new IOCP*/,
            0 /*completion key is ignored*/,
            0 /*concurrency is ignored with existing ports*/);
        if (!m_iocp) {
            std::cout << "Failed to bind event_channel " << event_channel << " to IOCP " << m_iocp <<
                ", err=" << GetLastError() << std::endl;
            return -1;
        }
        m_event_channel = event_channel;
        return 0;
    }
    std::cout << "Failed to bind event_channel " << event_channel << " to IOCP " << m_iocp <<
        ", err=" << GetLastError() << std::endl;
    return -1;
}
#endif

bool EventMgr::init(rmax_stream_id stream_id)
{
    rmax_status_t status;
    rmax_event_channel_t event_channel;

    status = rmax_get_event_channel(stream_id, &event_channel);
    if (status != RMAX_OK) {
        std::cout << "Failed getting event channel with rmax_get_event_channel, status:" << status << std::endl;
        return false;
    }

    int ret = bind_event_channel(event_channel);
    if (ret) {
        /* close event_channel */

        return false;
    }
    m_stream_id = stream_id;
    return true;
}

bool EventMgr::request_notification(rmax_stream_id stream_id)
{
    rmax_status_t ret;

#ifdef __linux__
    ret = rmax_request_notification(stream_id);
#else
    m_stream_id = stream_id;
    if (m_request_completed) {
        // Some other stream has received the event and notified the current stream by setting the
        // m_request_completed for the current stream_id to true.
        // This means that some chunks have been sent and are now ready for re-use, so we may skip
        // the request for notification and go on to aquire the next chunk in the calling function.
        m_request_completed = false;
        return false;
    }
    ret = rmax_request_notification(stream_id, &m_overlapped);
#endif
    if (ret == RMAX_OK) {
        wait_for_notification(stream_id);
    } else if (ret != RMAX_ERR_BUSY && ret != RMAX_SIGNAL) {
        std::cerr << "Error returned by rmax_request_notification(): " << ret << std::endl;
        rmax_cleanup();
        exit(-1);
    }
    return true;
}

int EventMgr::wait_for_notification(rmax_stream_id stream_id)
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


void set_cpu_affinity(const std::vector<int>& cpu_core_affinities)
{
    rmax_cpu_set_t cpu_affinity_mask;

    memset(&cpu_affinity_mask, 0, sizeof(cpu_affinity_mask));
    for (auto cpu : cpu_core_affinities) {
        if (cpu != CPU_NONE) {
            RMAX_CPU_SET(cpu, &cpu_affinity_mask);
        }
    }
    rt_set_thread_affinity(&cpu_affinity_mask);
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
    // We decrease one tick from the timestamp to prevent cases where the timestamp
    // lands up in the future due to calculation imprecision
    timestamp = std::fmod(timestamp, mask) - 1;
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
    strftime(time_str, sizeof(time_str) - 1, "%c", &time_buff);
#else
    localtime_s(&time_buff, &time_format);
    strftime(time_str, sizeof(time_str) - 1, "%c", &time_buff);
#endif
    return std::string(time_str);
}

