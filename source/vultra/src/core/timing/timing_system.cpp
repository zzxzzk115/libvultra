#include "vultra/core/timing/timing_system.hpp"

#include "vultra/core/base/common_context.hpp"

#include <algorithm>
#include <cmath>

namespace vultra
{
    bool TimingSystem::onInit()
    {
        VULTRA_CORE_INFO("[TimingSystem] Initializing...");

        VULTRA_CORE_TRACE("[TimingSystem] Providing ITimingService");
        ctx().services.provide<ITimingService>(this);

        VULTRA_CORE_INFO("[TimingSystem] Initialized!");
        return true;
    }

    void TimingSystem::onShutdown() { VULTRA_CORE_INFO("[TimingSystem] Shutting down"); }

    void TimingSystem::onPreUpdate(fsec dt)
    {
        m_UnscaledDeltaTime = std::clamp(dt.count(), 0.0f, m_MaxDeltaTime);
        m_UpdateDeltaTime   = m_UnscaledDeltaTime * m_TimeScale;
        m_SmoothedDeltaTime += (m_UnscaledDeltaTime - m_SmoothedDeltaTime) * m_DeltaSmoothingFactor;

        m_UnscaledTotalTime += m_UnscaledDeltaTime;
        m_TotalTime += m_UpdateDeltaTime;
        ++m_FrameIndex;

        ++m_FpsAccumulatedFrames;
        m_FpsAccumulatedTime += m_UnscaledDeltaTime;
        if (m_FpsAccumulatedTime >= 0.25f)
        {
            m_FramesPerSecond = m_FpsAccumulatedFrames / m_FpsAccumulatedTime;
            m_AverageFrameTime = m_FramesPerSecond > 0.0f ? (1.0f / m_FramesPerSecond) : 0.0f;
            m_FpsAccumulatedFrames = 0;
            m_FpsAccumulatedTime = 0.0f;
        }

        m_FixedStepsThisFrame = 0;
        m_FixedAlpha          = 0.0f;

        if (m_FixedDeltaTime <= 0.0f)
            return;

        m_FixedAccumulator += m_UpdateDeltaTime;

        const float rawStepCount = std::floor(m_FixedAccumulator / m_FixedDeltaTime);
        if (rawStepCount <= 0.0f)
        {
            m_FixedAlpha = std::clamp(m_FixedAccumulator / m_FixedDeltaTime, 0.0f, 1.0f);
            return;
        }

        const uint32_t steps = static_cast<uint32_t>(rawStepCount);
        m_FixedStepsThisFrame = std::min(steps, m_MaxFixedStepsPerFrame);

        m_FixedAccumulator -= static_cast<float>(m_FixedStepsThisFrame) * m_FixedDeltaTime;

        const float maxAccumulator = static_cast<float>(m_MaxFixedStepsPerFrame) * m_FixedDeltaTime;
        m_FixedAccumulator = std::clamp(m_FixedAccumulator, 0.0f, maxAccumulator);
        m_FixedAlpha       = std::clamp(m_FixedAccumulator / m_FixedDeltaTime, 0.0f, 1.0f);
    }

    void TimingSystem::setUpdateDeltaTime(float dt)
    {
        const float clamped = std::clamp(dt, 0.0f, m_MaxDeltaTime);
        m_UnscaledDeltaTime = clamped;
        m_UpdateDeltaTime   = clamped * m_TimeScale;
        m_SmoothedDeltaTime += (m_UnscaledDeltaTime - m_SmoothedDeltaTime) * m_DeltaSmoothingFactor;
    }

    void TimingSystem::setFixedDeltaTime(float dt)
    {
        m_FixedDeltaTime = std::max(1e-6f, dt);
        m_FixedAlpha     = std::clamp(m_FixedAccumulator / m_FixedDeltaTime, 0.0f, 1.0f);
    }

    void TimingSystem::setTimeScale(float scale)
    {
        m_TimeScale = std::max(0.0f, scale);
        m_UpdateDeltaTime = m_UnscaledDeltaTime * m_TimeScale;
    }

    void TimingSystem::setMaxFixedStepsPerFrame(uint32_t maxSteps)
    {
        m_MaxFixedStepsPerFrame = std::max(1u, maxSteps);
    }

    void TimingSystem::setMaxDeltaTime(float dt)
    {
        m_MaxDeltaTime = std::max(1e-6f, dt);
        m_UnscaledDeltaTime = std::clamp(m_UnscaledDeltaTime, 0.0f, m_MaxDeltaTime);
        m_UpdateDeltaTime   = m_UnscaledDeltaTime * m_TimeScale;
        m_SmoothedDeltaTime = std::clamp(m_SmoothedDeltaTime, 0.0f, m_MaxDeltaTime);
    }

    void TimingSystem::setDeltaSmoothingFactor(float factor)
    {
        m_DeltaSmoothingFactor = std::clamp(factor, 0.0f, 1.0f);
    }
} // namespace vultra
