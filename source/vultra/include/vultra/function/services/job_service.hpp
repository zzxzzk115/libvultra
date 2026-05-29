#pragma once

#include <vbase/service/service_registry.hpp>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace vultra
{
    enum class JobState : uint8_t
    {
        eQueued = 0,
        eRunning,
        eSucceeded,
        eFailed,
    };

    struct JobHandle
    {
        uint64_t id {0};
        explicit operator bool() const { return id != 0; }
    };

    struct JobSnapshot
    {
        JobHandle handle;
        JobState  state {JobState::eQueued};
        float     progress {0.0f};
        uint32_t  attempt {0};
        uint32_t  maxAttempts {1};
        std::string label;
        std::string message;
    };

    struct JobOptions
    {
        uint32_t maxAttempts {1};
    };

    class JobProgress
    {
    public:
        void setProgress(float progress, std::string message = {});
        void setMessage(std::string message);

    private:
        friend class IJobService;
        friend class JobSystem;

        struct SharedState
        {
            mutable std::mutex mutex;
            std::atomic<JobState> state {JobState::eQueued};
            float                 progress {0.0f};
            uint32_t              attempt {0};
            uint32_t              maxAttempts {1};
            std::string           label;
            std::string           message;
        };

        explicit JobProgress(std::shared_ptr<SharedState> state) : m_State(std::move(state)) {}

        std::shared_ptr<SharedState> m_State;
    };

    class IJobService
    {
    public:
        SERVICE_REGISTER(IJobService)

        using Fn = std::function<void(JobProgress&)>;

        virtual ~IJobService() = default;

        virtual JobHandle submit(std::string label, Fn fn) = 0;
        virtual JobHandle submit(std::string label, JobOptions options, Fn fn) = 0;
        virtual std::vector<JobSnapshot> snapshots() = 0;
        virtual void wait(JobHandle handle) = 0;
        virtual void waitAll() = 0;
    };
} // namespace vultra
