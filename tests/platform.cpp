#include "support/check.hpp"

#include <vultra/core/base/logger.hpp>
#include <vultra/platform/os/file.hpp>
#include <vultra/platform/os/memory.hpp>
#include <vultra/platform/os/process.hpp>

#include <atomic>
#include <chrono>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <thread>

namespace
{
    using support::must;
    using support::require;

    std::expected<void, vultra::Error> write(const std::filesystem::path& path, const std::string& contents)
    {
        return vultra::writeFileAtomically(path, std::as_bytes(std::span(contents)));
    }

    std::optional<std::string> tryRead(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
        {
            return std::nullopt;
        }
        return std::string {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    }

    std::string read(const std::filesystem::path& path)
    {
        auto contents = tryRead(path);
        require(contents.has_value(), "Cannot open published file");
        return std::move(*contents);
    }

    void atomicPublication(const std::filesystem::path& root)
    {
        const auto        path = root / std::filesystem::path(u8"asset é.bin");
        const std::string first(65536, 'a');
        const std::string second(131072, '\0');
        must(write(path, first));
        require(read(path) == first, "Initial publication changed the file contents");
        std::atomic_bool valid {true};
        {
            std::jthread reader(
                [&](std::stop_token stop)
                {
                    do
                    {
                        const auto contents = tryRead(path);
                        if (!contents || (*contents != first && *contents != second))
                        {
                            valid = false;
                        }
                    } while (!stop.stop_requested());
                });
            for (int i = 0; i < 32; ++i)
            {
                must(write(path, i % 2 ? first : second));
            }
        }
        require(valid, "Concurrent reader observed a missing or partially published file");
        require(read(path) == first, "Replacement publication changed the file contents");

        const auto blocked = root / "blocked";
        std::filesystem::create_directory(blocked);
        must(write(blocked / "original", first));
        support::rejects(write(blocked, second), "Publishing over a directory did not report a failure");
        require(read(blocked / "original") == first, "Failed publication changed the destination");
        must(write(root / "recovery", second));
        require(read(root / "recovery") == second, "Publication did not recover after failure");
        for (const auto& entry : std::filesystem::directory_iterator(root))
        {
            require(entry.path().extension() != ".tmp", "Publication left a temporary file behind");
        }
    }
} // namespace

int main(int argc, char** argv)
{
    const auto executable = must(vultra::executablePath());
    require(argc > 0 && executable.is_absolute() && std::filesystem::equivalent(executable, argv[0]),
            "Executable path does not identify the running binary");
    require(must(vultra::availablePhysicalMemory()) > 0, "Available physical memory was not reported");
    const auto root = std::filesystem::path("build/.tmp/platform") /
                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(root);
    atomicPublication(root);
    vultra::Logger::app().info("Platform tests passed: executable, memory, atomic publication, failure and recovery");
    return 0;
}
