#include <vultra/platform/os/process.hpp>

namespace vultra
{
    std::expected<std::filesystem::path, Error> executablePath()
    {
        std::error_code error;
        auto            path = std::filesystem::read_symlink("/proc/self/exe", error);
        if (error)
        {
            return makeError("Query executable path: {}", error.message());
        }
        return path;
    }
} // namespace vultra
