#include <vultra/platform/os/file_watcher.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
    namespace fs = std::filesystem;
    using namespace std::chrono_literals;

    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    void write(const fs::path& file, const char* text)
    {
        std::ofstream(file, std::ios::binary | std::ios::trunc) << text;
    }

    // Polls like a frame loop until the watcher has been quiet for a while, and returns everything reported.
    std::vector<fs::path> drain(vultra::FileWatcher& watcher)
    {
        std::vector<fs::path> reported;
        auto                  quietSince = std::chrono::steady_clock::now();
        const auto            deadline   = quietSince + 5s;
        while (std::chrono::steady_clock::now() < deadline)
        {
            const auto changes = watcher.poll();
            if (!changes.empty())
            {
                reported.insert(reported.end(), changes.begin(), changes.end());
                quietSince = std::chrono::steady_clock::now();
            }
            else if (!reported.empty() && std::chrono::steady_clock::now() - quietSince > 500ms)
            {
                break;
            }
            std::this_thread::sleep_for(16ms);
        }
        return reported;
    }

    size_t count(const std::vector<fs::path>& reported, const fs::path& file)
    {
        return size_t(std::ranges::count(reported, fs::absolute(file).lexically_normal()));
    }
} // namespace

int main()
try
{
    const auto root = fs::absolute("build/.tmp/file-watcher") /
                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    fs::create_directories(root / "shaders/lib");
    fs::create_directories(root / "shaders/.git");
    fs::create_directories(root / "other");
    const auto shader = root / "shaders/pass.slang";
    const auto nested = root / "shaders/lib/common.slangh";
    write(shader, "v1");
    write(nested, "v1");
    {
        vultra::FileWatcher watcher;
        const std::array    watched {root / "shaders", root / "missing"};
        watcher.watch(watched);
        std::this_thread::sleep_for(100ms);
        require(watcher.poll().empty(), "A new watcher reported changes before any edit");

        write(shader, "v2");
        auto reported = drain(watcher);
        require(count(reported, shader) == 1, "One edit must be reported exactly once");

        // An editor writing in several steps is one change.
        for (int step = 0; step < 6; ++step)
        {
            write(shader, step % 2 ? "burst-a" : "burst-b");
            std::this_thread::sleep_for(10ms);
        }
        reported = drain(watcher);
        require(count(reported, shader) == 1, "A burst of writes was not merged into one change");

        write(nested, "v2");
        reported = drain(watcher);
        require(count(reported, nested) == 1, "A change in a subdirectory was not reported");

        // Save through a temporary file and a rename.
        const auto temporary = root / "shaders/pass.slang.tmp";
        write(temporary, "v3");
        fs::rename(temporary, shader);
        reported = drain(watcher);
        require(count(reported, shader) >= 1, "A rename onto the watched file was not reported under its final name");

        fs::remove(shader);
        drain(watcher);
        write(shader, "v4");
        reported = drain(watcher);
        require(count(reported, shader) == 1, "A file recreated after deletion was not reported");

        write(root / "shaders/.git/index", "ignored");
        write(root / "other/unwatched.txt", "ignored");
        std::this_thread::sleep_for(400ms);
        require(watcher.poll().empty(), "An ignored or unwatched directory was reported");

        // Replacing the watched set stops reports from directories that are no longer in it.
        const std::array moved {root / "other"};
        watcher.watch(moved);
        write(shader, "v5");
        write(root / "other/unwatched.txt", "now watched");
        reported = drain(watcher);
        require(count(reported, shader) == 0 && count(reported, root / "other/unwatched.txt") == 1,
                "Replacing the watched directories did not take effect");
        write(root / "other/unwatched.txt", "pending when destroyed");
    }
    // The watcher is gone; further edits must not reach freed state.
    write(root / "other/unwatched.txt", "after destruction");
    std::this_thread::sleep_for(200ms);
    std::cout << "File watcher passed: single report, burst merge, subdirectories, rename, recreate, ignore rules "
                 "and destruction\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
