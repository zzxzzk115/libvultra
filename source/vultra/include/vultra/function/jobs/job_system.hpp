#pragma once

#include "vultra/core/engine/engine_subsystem.hpp"
#include "vultra/function/services/job_service.hpp"

#include <vtask/scheduler.hpp>
#include <vtask/task_set.hpp>

#include <memory>
#include <mutex>
#include <vector>

namespace vultra
{
    class JobSystem final : public EngineSubsystem, public IJobService
    {
    public:
        ENGINE_SUBSYSTEM(JobSystem)

        bool onInit() override;
        void onShutdown() override;
        void onUpdate(fsec dt) override;

        JobHandle submit(std::string label, Fn fn) override;
        JobHandle submit(std::string label, JobOptions options, Fn fn) override;
        std::vector<JobSnapshot> snapshots() override;
        void wait(JobHandle handle) override;
        void waitAll() override;

    private:
        struct JobRecord
        {
            JobHandle handle;
            std::shared_ptr<JobProgress::SharedState> state;
            std::unique_ptr<vtask::TaskSet> task;
        };

        void collectFinished();
        JobSnapshot snapshotOf(const JobRecord& record) const;

        vtask::Scheduler m_Scheduler;
        std::mutex       m_Mutex;
        uint64_t         m_NextJobId {1};
        std::vector<std::unique_ptr<JobRecord>> m_Jobs;
    };
} // namespace vultra
