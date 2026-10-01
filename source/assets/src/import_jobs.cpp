#include "import_jobs.hpp"

#include <vultra/core/base/logger.hpp>
#include <vultra/platform/os/memory.hpp>

#include <vtask/scheduler.hpp>
#include <vtask/task_set.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

namespace vultra::asset_detail
{
    void runImportJobs(std::string_view                     phase,
                       uint32_t                             count,
                       uint32_t                             workerLimit,
                       uint64_t                             scratchPerJob,
                       const std::function<void(uint32_t)>& job)
    {
        if (count == 0)
        {
            return;
        }
        const auto     hardware  = std::max(1u, std::thread::hardware_concurrency());
        const auto     requested = workerLimit == 0 ? hardware : std::min(workerLimit, hardware);
        const uint64_t budget    = availablePhysicalMemory() / 2;
        const auto workers = uint32_t(std::min({uint64_t(requested),
                                                uint64_t(count),
                                                std::max(uint64_t(1), budget / std::max(uint64_t(1), scratchPerJob))}));
        Logger::core().info("{}: 0/{}; {} vtask workers, estimated scratch {} MiB/job, budget {} MiB",
                            phase,
                            count,
                            workers,
                            scratchPerJob / (1024 * 1024),
                            budget / (1024 * 1024));
        const auto                      started   = std::chrono::steady_clock::now();
        auto                            lastLog   = started;
        uint32_t                        completed = 0;
        std::mutex                      progress;
        std::atomic_bool                failed = false;
        std::vector<std::exception_ptr> errors(count);
        vtask::Scheduler                scheduler(workers);
        vtask::TaskSet                  tasks(
            count,
            1,
            [&](vtask::Range range)
            {
                for (uint32_t i = range.begin; i < range.end && !failed.load(std::memory_order_relaxed); ++i)
                {
                    try
                    {
                        job(i);
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
                    catch (...)
                    {
                        errors[i] = std::current_exception();
                        failed.store(true, std::memory_order_relaxed);
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
                std::rethrow_exception(errors[i]);
            }
        }
    }
} // namespace vultra::asset_detail
