#include <vultra/platform/os/memory.hpp>

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace vultra
{
    uint64_t availablePhysicalMemory()
    {
        std::ifstream stream("/proc/meminfo");
        std::string   line;
        while (std::getline(stream, line))
        {
            if (line.starts_with("MemAvailable:"))
            {
                std::istringstream fields(line);
                std::string        name;
                uint64_t           kibibytes = 0;
                std::string        unit;
                if (fields >> name >> kibibytes >> unit && unit == "kB")
                {
                    return kibibytes * 1024;
                }
                break;
            }
        }
        throw std::runtime_error("Query available physical memory: cannot read MemAvailable from /proc/meminfo");
    }
} // namespace vultra
