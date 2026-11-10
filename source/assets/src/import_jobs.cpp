#include "import_jobs.hpp"

#include <vultra/core/base/logger.hpp>
#include <vultra/platform/os/memory.hpp>

#include <vtask/scheduler.hpp>
#include <vtask/task_set.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <format>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace vultra::asset_detail
{
    std::expected<void, std::string>
    runImportJobsExpected(std::string_view                                                 phase,
                          uint32_t                                                         count,
                          uint32_t                                                         workerLimit,
                          uint64_t                                                         scratchPerJob,
                          const std::function<std::expected<void, std::string>(uint32_t)>& job)
    {
        if (count == 0)
        {
            return {};
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
        std::optional<std::pair<uint32_t, std::string>> error;
        vtask::Scheduler                scheduler(workers);
        vtask::TaskSet                  tasks(
            count,
            1,
            [&](vtask::Range range)
            {
                for (uint32_t i = range.begin; i < range.end && !failed.load(std::memory_order_relaxed); ++i)
                {
                    auto result = job(i);
                    if (!result)
                    {
                        failed.store(true, std::memory_order_relaxed);
                        const std::lock_guard lock(progress);
                        if (!error || i < error->first)
                        {
                            error.emplace(i, std::move(result.error()));
                        }
                        break;
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
        if (error)
        {
            return std::unexpected(std::format("{} failed at job {}: {}", phase, error->first, error->second));
        }
        return {};
    }

    void runImportJobs(std::string_view                     phase,
                       uint32_t                             count,
                       uint32_t                             workerLimit,
                       uint64_t                             scratchPerJob,
                       const std::function<void(uint32_t)>& job)
    {
        std::mutex                                             errorMutex;
        std::optional<std::pair<uint32_t, std::exception_ptr>> error;
        const auto                                             result = runImportJobsExpected(phase,
                                                  count,
                                                  workerLimit,
                                                  scratchPerJob,
                                                  [&](uint32_t i) -> std::expected<void, std::string>
                                                  {
                                                      try
                                                      {
                                                          job(i);
                                                          return {};
                                                      }
                                                      catch (...)
                                                      {
                                                          const std::lock_guard lock(errorMutex);
                                                          if (!error || i < error->first)
                                                          {
                                                              error.emplace(i, std::current_exception());
                                                          }
                                                          return std::unexpected("Legacy import job threw");
                                                      }
                                                  });
        if (!result && error)
        {
            Logger::core().error("{}", result.error());
            std::rethrow_exception(error->second);
        }
    }
} // namespace vultra::asset_detail
