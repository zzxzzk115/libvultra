#include <vultra/platform/os/memory.hpp>

#include <Windows.h>
#include <system_error>

namespace vultra
{
    std::expected<uint64_t, Error> availablePhysicalMemory()
    {
        MEMORYSTATUSEX status {};
        status.dwLength = sizeof(status);
        if (!GlobalMemoryStatusEx(&status))
        {
            return makeError("Query available physical memory: {}",
                             std::error_code(int(GetLastError()), std::system_category()).message());
        }
        return status.ullAvailPhys;
    }
} // namespace vultra
