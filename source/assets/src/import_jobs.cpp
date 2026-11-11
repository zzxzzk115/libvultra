#include "import_jobs.hpp"

#include <vultra/core/base/logger.hpp>
#include <vultra/platform/os/memory.hpp>

#include <vtask/scheduler.hpp>
#include <vtask/task_set.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace vultra::asset_detail
{
    std::expected<void, Error> runImportJobs(std::string_view                                           phase,
                                             uint32_t                                                   count,
                                             uint32_t                                                   workerLimit,
                                             uint64_t                                                   scratchPerJob,
                                             const std::function<std::expected<void, Error>(uint32_t)>& job)
    {
        if (count == 0)
        {
            return {};
        }
        const auto memory = availablePhysicalMemory();
        if (!memory)
        {
            return withContext(phase, memory.error());
        }
        const auto     hardware  = std::max(1u, std::thread::hardware_concurrency());
        const auto     requested = workerLimit == 0 ? hardware : std::min(workerLimit, hardware);
        const uint64_t budget    = *memory / 2;
        const auto workers = uint32_t(std::min({uint64_t(requested),
                                                uint64_t(count),
                                                std::max(uint64_t(1), budget / std::max(uint64_t(1), scratchPerJob))}));
        Logger::core().info("{}: 0/{}; {} vtask workers, estimated scratch {} MiB/job, budget {} MiB",
                            phase,
                            count,
                            workers,
                            scratchPerJob / (1024 * 1024),
                            budget / (1024 * 1024));
        const auto                        started   = std::chrono::steady_clock::now();
        auto                              lastLog   = started;
        uint32_t                          completed = 0;
        std::mutex                        progress;
        std::atomic_bool                  failed = false;
        std::vector<std::optional<Error>> errors(count);
        vtask::Scheduler                  scheduler(workers);
        vtask::TaskSet                    tasks(
            count,
            1,
            [&](vtask::Range range)
            {
                for (uint32_t i = range.begin; i < range.end && !failed.load(std::memory_order_relaxed); ++i)
                {
                    if (auto result = job(i); !result)
                    {
                        errors[i] = std::move(result.error());
                        failed.store(true, std::memory_order_relaxed);
                        continue;
                    }
                    const std::lock_guard lock(progress);
                    ++completed;
                    const auto now = std::chrono::steady_clock::now();
                    if (completed == 1 || completed == count || now - lastLog >= std::chrono::milliseconds(500))
                    {
                        Logger::core().info("{}: {}/{} ({:.1f} ms)",
                                            phase,
                                            completed,
                                            count,
                                            std::chrono::duration<double, std::milli>(now - started).count());
                        lastLog = now;
                    }
                }
            });
        scheduler.run(tasks);
        scheduler.wait(tasks);
        for (uint32_t i = 0; i < errors.size(); ++i)
        {
            if (errors[i])
            {
                Logger::core().error("{} failed at job {}", phase, i);
                return std::unexpected(std::move(*errors[i]));
            }
        }
        return {};
    }
} // namespace vultra::asset_detail
