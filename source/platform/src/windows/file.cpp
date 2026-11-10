#include <vultra/platform/os/file.hpp>

#include <Windows.h>
#include <atomic>
#include <chrono>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <system_error>

namespace vultra
{
    std::expected<void, std::string> writeFileAtomicallyExpected(const std::filesystem::path& path,
                                                                 std::span<const std::byte>   bytes)
    {
        if (bytes.size() > uint64_t(std::numeric_limits<std::streamsize>::max()))
        {
            return std::unexpected("Atomic file exceeds stream size: " + path.string());
        }
        static std::atomic_uint64_t counter {0};
        auto                        temporary = path;
        temporary += "." + std::to_string(GetCurrentProcessId()) + "." +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "." +
                     std::to_string(counter.fetch_add(1)) + ".tmp";
        std::ofstream stream;
        const auto    fail = [&](std::string error) -> std::expected<void, std::string>
        {
            if (stream.is_open())
            {
                stream.close();
            }
            std::error_code cleanup;
            std::filesystem::remove(temporary, cleanup);
            if (cleanup)
            {
                error += "; remove temporary file: " + cleanup.message();
            }
            return std::unexpected(std::move(error));
        };
        stream.open(temporary, std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            return fail("Open temporary file: " + temporary.string());
        }
        stream.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        if (!stream)
        {
            return fail("Write temporary file: " + temporary.string());
        }
        stream.close();
        if (!stream)
        {
            return fail("Close temporary file: " + temporary.string());
        }
        // The temporary file shares its destination directory; publishing never exposes a partial write.
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            const std::error_code error(int(GetLastError()), std::system_category());
            return fail("Publish file " + path.string() + ": " + error.message());
        }
        return {};
    }

    void writeFileAtomically(const std::filesystem::path& path, std::span<const std::byte> bytes)
    {
        if (auto result = writeFileAtomicallyExpected(path, bytes); !result)
        {
            throw std::runtime_error(result.error());
        }
    }
} // namespace vultra
