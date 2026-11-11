#include <vultra/platform/os/memory.hpp>

#include <mach/mach.h>

namespace vultra
{
    std::expected<uint64_t, Error> availablePhysicalMemory()
    {
        vm_statistics64_data_t statistics {};
        mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
        const auto             host  = mach_host_self();
        const auto             result =
            host_statistics64(host, HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&statistics), &count);
        vm_size_t pageSize = 0;
        host_page_size(host, &pageSize);
        mach_port_deallocate(mach_task_self(), host);
        if (result != KERN_SUCCESS)
        {
            return makeError("Query available physical memory: {}", mach_error_string(result));
        }
        // Inactive and speculative pages are reclaimable without swapping, like Linux MemAvailable.
        const uint64_t pages =
            uint64_t(statistics.free_count) + statistics.inactive_count + statistics.speculative_count;
        return pages * pageSize;
    }
} // namespace vultra
