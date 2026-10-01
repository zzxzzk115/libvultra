#include <vultra/core/base/logger.hpp>
#include <vultra/platform/os/file.hpp>
#include <vultra/platform/os/memory.hpp>
#include <vultra/platform/os/process.hpp>

#include <atomic>
#include <chrono>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    void write(const std::filesystem::path& path, const std::string& contents)
    {
        vultra::writeFileAtomically(path, std::as_bytes(std::span(contents)));
    }

    std::string read(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        require(bool(stream), "Cannot open published file");
        return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    }

    void atomicPublication(const std::filesystem::path& root)
    {
        const auto        path = root / std::filesystem::path(u8"asset é.bin");
        const std::string first(65536, 'a');
        const std::string second(131072, '\0');
        write(path, first);
        require(read(path) == first, "Initial publication changed the file contents");
        std::atomic_bool valid {true};
        {
            std::jthread reader(
                [&](std::stop_token stop)
                {
                    do
                    {
                        try
                        {
                            const auto contents = read(path);
                            if (contents != first && contents != second)
                            {
                                valid = false;
                            }
                        }
                        catch (const std::exception&)
                        {
                            valid = false;
                        }
                    } while (!stop.stop_requested());
                });
            for (int i = 0; i < 32; ++i)
            {
                write(path, i % 2 ? first : second);
            }
        }
        require(valid, "Concurrent reader observed a missing or partially published file");
        require(read(path) == first, "Replacement publication changed the file contents");

        const auto blocked = root / "blocked";
        std::filesystem::create_directory(blocked);
        write(blocked / "original", first);
        bool rejected = false;
        try
        {
            write(blocked, second);
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        require(rejected, "Publishing over a directory did not report a failure");
        require(read(blocked / "original") == first, "Failed publication changed the destination");
        write(root / "recovery", second);
        require(read(root / "recovery") == second, "Publication did not recover after failure");
        for (const auto& entry : std::filesystem::directory_iterator(root))
        {
            require(entry.path().extension() != ".tmp", "Publication left a temporary file behind");
        }
    }
} // namespace

int main(int argc, char** argv)
try
{
    const auto executable = vultra::executablePath();
    require(argc > 0 && executable.is_absolute() && std::filesystem::equivalent(executable, argv[0]),
            "Executable path does not identify the running binary");
    require(vultra::availablePhysicalMemory() > 0, "Available physical memory was not reported");
    const auto root = std::filesystem::path("build/.tmp/platform") /
                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(root);
    atomicPublication(root);
    vultra::Logger::app().info("Platform tests passed: executable, memory, atomic publication, failure and recovery");
    return 0;
}
catch (const std::exception& error)
{
    vultra::Logger::app().error("{}", error.what());
    return 1;
}
