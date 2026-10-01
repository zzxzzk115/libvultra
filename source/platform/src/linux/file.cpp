#include <vultra/platform/os/file.hpp>

#include <atomic>
#include <chrono>
#include <fstream>
#include <unistd.h>

namespace vultra
{
    void writeFileAtomically(const std::filesystem::path& path, std::span<const std::byte> bytes)
    {
        static std::atomic_uint64_t counter {0};
        auto                        temporary = path;
        temporary += "." + std::to_string(getpid()) + "." +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "." +
                     std::to_string(counter.fetch_add(1)) + ".tmp";
        try
        {
            std::ofstream stream;
            stream.exceptions(std::ios::failbit | std::ios::badbit);
            stream.open(temporary, std::ios::binary | std::ios::trunc);
            stream.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
            stream.close();
            // The temporary file shares the destination directory, so POSIX rename is atomic.
            std::filesystem::rename(temporary, path);
        }
        catch (...)
        {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            throw;
        }
    }
} // namespace vultra
