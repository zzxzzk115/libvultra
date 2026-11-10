#include <cerrno>
#include <pthread.h>

namespace
{
    static int threadCalls = 0;
    static int rejectCall  = 0;

    static int
    createTestThread(pthread_t* thread, const pthread_attr_t* attributes, void* (*entry)(void*), void* context)
    {
        if (++threadCalls == rejectCall)
        {
            return EAGAIN;
        }
        return pthread_create(thread, attributes, entry, context);
    }

} // namespace

// This executable instantiates only the vendored header, isolating both native start failures.
#define pthread_create createTestThread
#include <FileWatch.hpp>
#undef pthread_create

#if defined(__APPLE__)
#include <libproc.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

static size_t descriptors()
{
#if defined(__APPLE__)
    const int bytes = proc_pidinfo(getpid(), PROC_PIDLISTFDS, 0, nullptr, 0);
    if (bytes <= 0)
    {
        std::abort();
    }
    std::vector<proc_fdinfo> result(size_t(bytes) / sizeof(proc_fdinfo) + 16);
    const int                read =
        proc_pidinfo(getpid(), PROC_PIDLISTFDS, 0, result.data(), int(result.size() * sizeof(proc_fdinfo)));
    if (read <= 0)
    {
        std::abort();
    }
    result.resize(size_t(read) / sizeof(proc_fdinfo));
    return std::ranges::count(result, PROX_FDTYPE_VNODE, &proc_fdinfo::proc_fdtype);
#else
    return size_t(std::ranges::distance(std::filesystem::directory_iterator("/proc/self/fd")));
#endif
}

int main()
{
    using Watch     = filewatch::FileWatch<std::string>;
    const auto root = std::filesystem::path("build/.tmp/file-watch-expected") /
                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(root);
    const auto ignore = [](const auto&, auto) -> std::expected<void, std::string>
    {
        return {};
    };
    {
        auto warm = Watch::createExpected(root.string(), ignore);
        if (!warm)
        {
            return 1;
        }
    }
    const auto before = descriptors();
    for (int failure : {1, 2})
    {
        for (int round = 0; round < 64; ++round)
        {
            threadCalls = 0;
            rejectCall  = failure;
            auto failed = Watch::createExpected(root.string(), ignore);
            if (failed || failed.error().find("Create watch thread:") == std::string::npos)
            {
                return 2;
            }
        }
    }
    if (descriptors() != before)
    {
        return 3;
    }
    rejectCall = 0;
    std::atomic_uint32_t events {0};
    {
        auto recovered = Watch::createExpected(root.string(),
                                               [&](const auto&, auto) -> std::expected<void, std::string>
                                               {
                                                   ++events;
                                                   return {};
                                               });
        if (!recovered)
        {
            return 4;
        }
        {
            std::ofstream output(root / "shader");
            output << "recovered";
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!events && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (!events || !(*recovered)->statusExpected())
        {
            return 5;
        }
    }
#if defined(__linux__)
    const auto deleted = root / "deleted-watch";
    std::filesystem::create_directory(deleted);
    {
        auto watched = Watch::createExpected(deleted.string(), ignore);
        if (!watched)
        {
            return 7;
        }
        std::filesystem::remove(deleted);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while ((*watched)->statusExpected() && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        const auto status = (*watched)->statusExpected();
        if (status || status.error() != "Inotify watch was removed")
        {
            return 8;
        }
    }
    std::filesystem::create_directory(deleted);
    {
        auto recovered = Watch::createExpected(deleted.string(), ignore);
        if (!recovered || !(*recovered)->statusExpected())
        {
            return 9;
        }
    }
#endif
    if (descriptors() != before)
    {
        return 6;
    }
    std::filesystem::remove_all(root);
    std::puts(
        "Expected native threads: 128 first/second-start failures, descriptor retention and event recovery passed");
}
