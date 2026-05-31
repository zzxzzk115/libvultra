#include "vultra/function/jobs/job_system.hpp"

#include "vultra/core/base/common_context.hpp"

#include <algorithm>
#include <exception>
#include <format>
#include <utility>

namespace vultra
{
    void JobProgress::setProgress(float progress, std::string message)
    {
        if (!m_State)
            return;
        std::scoped_lock lock(m_State->mutex);
        m_State->progress = std::clamp(progress, 0.0f, 1.0f);
        if (!message.empty())
            m_State->message = std::move(message);
    }

    void JobProgress::setMessage(std::string message)
    {
        if (!m_State)
            return;
        std::scoped_lock lock(m_State->mutex);
        m_State->message = std::move(message);
    }

    bool JobSystem::onInit()
    {
        VULTRA_CORE_TRACE("[JobSystem] Providing IJobService");
        ctx().services.provide<IJobService>(this);
        return true;
    }

    void JobSystem::onShutdown()
    {
        waitAll();
        std::scoped_lock lock(m_Mutex);
        m_Jobs.clear();
    }

    void JobSystem::onUpdate(fsec) { collectFinished(); }

    JobHandle JobSystem::submit(std::string label, Fn fn)
    {
        return submit(std::move(label), JobOptions {}, std::move(fn));
    }

    JobHandle JobSystem::submit(std::string label, JobOptions options, Fn fn)
    {
        options.maxAttempts = std::max(1u, options.maxAttempts);

        auto state = std::make_shared<JobProgress::SharedState>();
        {
            std::scoped_lock lock(state->mutex);
            state->label       = std::move(label);
            state->message     = state->label;
            state->maxAttempts = options.maxAttempts;
        }

        auto record   = std::make_unique<JobRecord>();
        record->state = state;
        {
            std::scoped_lock lock(m_Mutex);
            record->handle = JobHandle {m_NextJobId++};
        }

        auto* statePtr = state.get();
        record->task   = std::make_unique<vtask::TaskSet>(1, 1, [state, options, fn = std::move(fn)](vtask::Range) mutable {
            JobProgress progress {state};
            for (uint32_t attempt = 1; attempt <= options.maxAttempts; ++attempt)
            {
                {
                    std::scoped_lock lock(state->mutex);
                    state->attempt  = attempt;
                    state->progress = 0.0f;
                    if (options.maxAttempts > 1)
                        state->message = std::format("{} (attempt {}/{})", state->label, attempt, options.maxAttempts);
                }
                state->state.store(JobState::eRunning, std::memory_order_release);

                try
                {
                    fn(progress);
                    progress.setProgress(1.0f);
                    state->state.store(JobState::eSucceeded, std::memory_order_release);
                    return;
                }
                catch (const std::exception& error)
                {
                    progress.setMessage(std::format("Job failed: {}", error.what()));
                }
                catch (...)
                {
                    progress.setMessage("Job failed.");
                }
            }

            state->state.store(JobState::eFailed, std::memory_order_release);
        });

        JobHandle handle = record->handle;
        m_Scheduler.run(*record->task);

        {
            std::scoped_lock lock(m_Mutex);
            m_Jobs.push_back(std::move(record));
        }
        (void)statePtr;
        return handle;
    }

    JobSnapshot JobSystem::snapshotOf(const JobRecord& record) const
    {
        JobSnapshot snapshot;
        snapshot.handle = record.handle;
        snapshot.state  = record.state->state.load(std::memory_order_acquire);
        std::scoped_lock lock(record.state->mutex);
        snapshot.progress    = record.state->progress;
        snapshot.attempt     = record.state->attempt;
        snapshot.maxAttempts = record.state->maxAttempts;
        snapshot.label       = record.state->label;
        snapshot.message     = record.state->message;
        return snapshot;
    }

    std::vector<JobSnapshot> JobSystem::snapshots()
    {
        collectFinished();
        std::vector<JobSnapshot> out;
        std::scoped_lock lock(m_Mutex);
        out.reserve(m_Jobs.size());
        for (const auto& job : m_Jobs)
            out.push_back(snapshotOf(*job));
        return out;
    }

    void JobSystem::wait(const JobHandle handle)
    {
        if (!handle)
            return;

        JobRecord* target = nullptr;
        {
            std::scoped_lock lock(m_Mutex);
            for (auto& job : m_Jobs)
            {
                if (job->handle.id == handle.id)
                {
                    target = job.get();
                    break;
                }
            }
        }

        if (target && target->task)
            m_Scheduler.wait(*target->task);
        collectFinished();
    }

    void JobSystem::waitAll()
    {
        std::vector<JobRecord*> jobs;
        {
            std::scoped_lock lock(m_Mutex);
            jobs.reserve(m_Jobs.size());
            for (auto& job : m_Jobs)
                jobs.push_back(job.get());
        }

        for (auto* job : jobs)
        {
            if (job && job->task)
                m_Scheduler.wait(*job->task);
        }
        collectFinished();
    }

    uint32_t JobSystem::concurrency() const { return m_Scheduler.concurrency(); }

    void JobSystem::collectFinished()
    {
        std::scoped_lock lock(m_Mutex);
        for (auto it = m_Jobs.begin(); it != m_Jobs.end();)
        {
            const auto state = (*it)->state->state.load(std::memory_order_acquire);
            if (state != JobState::eSucceeded && state != JobState::eFailed)
            {
                ++it;
                continue;
            }

            if ((*it)->task)
                m_Scheduler.wait(*(*it)->task);
            it = m_Jobs.erase(it);
        }
    }
} // namespace vultra
