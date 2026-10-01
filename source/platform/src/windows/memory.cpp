#include <vultra/platform/os/memory.hpp>

#include <Windows.h>
#include <system_error>

namespace vultra
{
    uint64_t availablePhysicalMemory()
    {
        MEMORYSTATUSEX status {};
        status.dwLength = sizeof(status);
        if (!GlobalMemoryStatusEx(&status))
        {
            throw std::system_error(int(GetLastError()), std::system_category(), "Query available physical memory");
        }
        return status.ullAvailPhys;
    }
} // namespace vultra
