#include <vultra/platform/os/file.hpp>

#include <Windows.h>
#include <atomic>
#include <chrono>
#include <fstream>
#include <system_error>
#include <thread>

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
        // Another process may hold the destination open for a moment without delete sharing, as an antivirus
        // scanner does with a file just written; replacing it then fails with access denied or a sharing violation.
        // Retry for about half a second before reporting the failure.
        constexpr int kAttempts = 20;
        for (int attempt = 1;; ++attempt)
        {
            if (MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            {
                return {};
            }
            const DWORD code = GetLastError();
            if ((code != ERROR_ACCESS_DENIED && code != ERROR_SHARING_VIOLATION) || attempt == kAttempts)
            {
                const std::error_code error(int(code), std::system_category());
                std::filesystem::remove(temporary, ignored);
                return makeError("Publish {}: {}", path.string(), error.message());
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
    }
} // namespace vultra
