#include <vultra/platform/os/process.hpp>

#include <array>
#include <stdexcept>
#include <system_error>
#include <windows.h>

namespace vultra
{
    std::filesystem::path executablePath()
    {
        std::array<wchar_t, 32768> path {};
        const auto                 length = GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()));
        if (!length)
        {
            throw std::system_error(int(GetLastError()), std::system_category(), "Get executable path");
        }
        if (length == path.size())
        {
            throw std::runtime_error("Executable path exceeds the Windows path limit");
        }
        return std::filesystem::path(path.data(), path.data() + length);
    }
} // namespace vultra
