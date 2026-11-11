#include <vultra/platform/os/file.hpp>

#include <Windows.h>
#include <atomic>
#include <chrono>
#include <fstream>
#include <system_error>

namespace vultra
{
    std::expected<void, Error> writeFileAtomically(const std::filesystem::path& path, std::span<const std::byte> bytes)
    {
        static std::atomic_uint64_t counter {0};
        auto                        temporary = path;
        temporary += "." + std::to_string(GetCurrentProcessId()) + "." +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "." +
                     std::to_string(counter.fetch_add(1)) + ".tmp";
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        stream.close();
        std::error_code ignored;
        if (!stream)
        {
            std::filesystem::remove(temporary, ignored);
            return makeError("Write {}: cannot write the temporary file", path.string());
        }
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            const std::error_code error(int(GetLastError()), std::system_category());
            std::filesystem::remove(temporary, ignored);
            return makeError("Publish asset cache: {}", error.message());
        }
        return {};
    }
} // namespace vultra
