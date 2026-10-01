#include <vultra/platform/os/file.hpp>

#include <Windows.h>
#include <atomic>
#include <chrono>
#include <fstream>
#include <system_error>

namespace vultra
{
    void writeFileAtomically(const std::filesystem::path& path, std::span<const std::byte> bytes)
    {
        static std::atomic_uint64_t counter {0};
        auto                        temporary = path;
        temporary += "." + std::to_string(GetCurrentProcessId()) + "." +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "." +
                     std::to_string(counter.fetch_add(1)) + ".tmp";
        try
        {
            std::ofstream stream;
            stream.exceptions(std::ios::failbit | std::ios::badbit);
            stream.open(temporary, std::ios::binary | std::ios::trunc);
            stream.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
            stream.close();
            if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            {
                throw std::system_error(int(GetLastError()), std::system_category(), "Publish asset cache");
            }
        }
        catch (...)
        {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            throw;
        }
    }
} // namespace vultra
