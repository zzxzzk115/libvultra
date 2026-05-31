#include "common/system_memory.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <mach/task.h>
#include <unistd.h>
#elif defined(__linux__)
#include <sys/sysinfo.h>
#include <unistd.h>

#include <cstdio>
#endif

namespace vultra_app
{
    SystemMemorySnapshot querySystemMemory()
    {
        SystemMemorySnapshot snapshot {};

#if defined(_WIN32)
        MEMORYSTATUSEX memoryStatus {};
        memoryStatus.dwLength = sizeof(memoryStatus);
        if (GlobalMemoryStatusEx(&memoryStatus))
        {
            snapshot.systemMemoryAvailable = true;
            snapshot.systemAvailableBytes  = static_cast<uint64_t>(memoryStatus.ullAvailPhys);
            snapshot.systemTotalBytes      = static_cast<uint64_t>(memoryStatus.ullTotalPhys);
        }

        PROCESS_MEMORY_COUNTERS_EX counters {};
        if (GetProcessMemoryInfo(GetCurrentProcess(),
                                 reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                                 sizeof(counters)))
        {
            snapshot.processResidentAvailable = true;
            snapshot.processResidentBytes     = static_cast<uint64_t>(counters.WorkingSetSize);
        }
#elif defined(__APPLE__)
        mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
        mach_task_basic_info_data_t taskInfo {};
        if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&taskInfo), &count) ==
            KERN_SUCCESS)
        {
            snapshot.processResidentAvailable = true;
            snapshot.processResidentBytes     = static_cast<uint64_t>(taskInfo.resident_size);
        }

        mach_port_t host = mach_host_self();
        vm_size_t pageSize = 0;
        if (host_page_size(host, &pageSize) == KERN_SUCCESS)
        {
            vm_statistics64_data_t stats {};
            mach_msg_type_number_t statsCount = HOST_VM_INFO64_COUNT;
            if (host_statistics64(host, HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&stats), &statsCount) ==
                KERN_SUCCESS)
            {
                const uint64_t freePages =
                    static_cast<uint64_t>(stats.free_count) + static_cast<uint64_t>(stats.inactive_count);
                snapshot.systemMemoryAvailable = true;
                snapshot.systemAvailableBytes  = freePages * static_cast<uint64_t>(pageSize);
            }
        }

        const long pageCount = sysconf(_SC_PHYS_PAGES);
        const long pageBytes = sysconf(_SC_PAGE_SIZE);
        if (pageCount > 0 && pageBytes > 0)
            snapshot.systemTotalBytes = static_cast<uint64_t>(pageCount) * static_cast<uint64_t>(pageBytes);
#elif defined(__linux__)
        sysinfo info {};
        if (sysinfo(&info) == 0)
        {
            snapshot.systemMemoryAvailable = true;
            snapshot.systemAvailableBytes  = static_cast<uint64_t>(info.freeram) * static_cast<uint64_t>(info.mem_unit);
            snapshot.systemTotalBytes      = static_cast<uint64_t>(info.totalram) * static_cast<uint64_t>(info.mem_unit);
        }

        if (FILE* statm = std::fopen("/proc/self/statm", "r"))
        {
            unsigned long sizePages = 0;
            unsigned long residentPages = 0;
            if (std::fscanf(statm, "%lu %lu", &sizePages, &residentPages) == 2)
            {
                const long pageBytes = sysconf(_SC_PAGESIZE);
                if (pageBytes > 0)
                {
                    snapshot.processResidentAvailable = true;
                    snapshot.processResidentBytes =
                        static_cast<uint64_t>(residentPages) * static_cast<uint64_t>(pageBytes);
                }
            }
            std::fclose(statm);
        }
#endif

        return snapshot;
    }
} // namespace vultra_app
