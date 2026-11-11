#include <vultra/platform/os/process.hpp>

#include <array>
#include <system_error>
#include <windows.h>

namespace vultra
{
    std::expected<std::filesystem::path, Error> executablePath()
    {
        std::array<wchar_t, 32768> path {};
        const auto                 length = GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()));
        if (!length)
        {
            return makeError("Get executable path: {}",
                             std::error_code(int(GetLastError()), std::system_category()).message());
        }
        if (length == path.size())
        {
            return makeError("Executable path exceeds the Windows path limit");
        }
        return std::filesystem::path(path.data(), path.data() + length);
    }
} // namespace vultra
