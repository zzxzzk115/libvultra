#include <vultra/platform/os/process.hpp>

namespace vultra
{
    std::filesystem::path executablePath()
    {
        return std::filesystem::read_symlink("/proc/self/exe");
    }
} // namespace vultra
