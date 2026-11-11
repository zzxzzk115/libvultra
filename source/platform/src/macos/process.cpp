#include <vultra/platform/os/process.hpp>

#include <mach-o/dyld.h>

#include <string>

namespace vultra
{
    std::expected<std::filesystem::path, Error> executablePath()
    {
        uint32_t size = 0;
        _NSGetExecutablePath(nullptr, &size);
        std::string path(size, '\0');
        if (_NSGetExecutablePath(path.data(), &size) != 0)
        {
            return makeError("Query executable path: _NSGetExecutablePath failed");
        }
        path.resize(path.find('\0'));
        // dyld may report a path through symlinks; /proc/self/exe on Linux is already resolved.
        std::error_code error;
        auto            canonical = std::filesystem::canonical(path, error);
        if (error)
        {
            return makeError("Query executable path: {}: {}", path, error.message());
        }
        return canonical;
    }
} // namespace vultra
