#include <vultra/core/base/logger.hpp>
#include <vultra/platform/os/process.hpp>

#include <libproc.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <unistd.h>

namespace vultra
{
    std::filesystem::path executablePath()
    {
        std::array<char, PROC_PIDPATHINFO_MAXSIZE> path {};
        if (proc_pidpath(getpid(), path.data(), uint32_t(path.size())) <= 0)
        {
            Logger::core().error("Cannot read executable path: {}", std::strerror(errno));
            return {};
        }
        return std::filesystem::path(path.data());
    }
} // namespace vultra
