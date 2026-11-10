#include <vultra/platform/os/memory.hpp>

#include <mach/mach.h>

#include <stdexcept>
#include <string>

namespace vultra
{
    uint64_t availablePhysicalMemory()
    {
        const auto             host       = mach_host_self();
        vm_size_t              pageSize   = 0;
        const auto             pageResult = host_page_size(host, &pageSize);
        vm_statistics64_data_t statistics {};
        mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
        const auto             result =
            host_statistics64(host, HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&statistics), &count);
        mach_port_deallocate(mach_task_self(), host);
        if (pageResult != KERN_SUCCESS || result != KERN_SUCCESS)
        {
            throw std::runtime_error("Query available physical memory: " +
                                     std::string(mach_error_string(pageResult != KERN_SUCCESS ? pageResult : result)));
        }
        return (uint64_t(statistics.free_count) + statistics.inactive_count) * pageSize;
    }
} // namespace vultra
