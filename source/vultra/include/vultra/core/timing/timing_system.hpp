#pragma once

#include "vultra/core/engine/engine_subsystem.hpp"
#include "vultra/core/services/timing_service.hpp"

namespace vultra
{
    class TimingSystem final : public EngineSubsystem, public ITimingService
    {
    public:
        ENGINE_SUBSYSTEM(TimingSystem)

        float updateDeltaTime() const override { return m_UpdateDeltaTime; }
        float fixedDeltaTime() const override { return m_FixedDeltaTime; }
        float unscaledDeltaTime() const override { return m_UnscaledDeltaTime; }
        float smoothedDeltaTime() const override { return m_SmoothedDeltaTime; }

        float totalTime() const override { return m_TotalTime; }
        float unscaledTotalTime() const override { return m_UnscaledTotalTime; }
        float averageFrameTime() const override { return m_AverageFrameTime; }
        float framesPerSecond() const override { return m_FramesPerSecond; }

        float timeScale() const override { return m_TimeScale; }
        float fixedAlpha() const override { return m_FixedAlpha; }
        uint32_t fixedStepsThisFrame() const override { return m_FixedStepsThisFrame; }
        uint64_t frameIndex() const override { return m_FrameIndex; }
        float maxDeltaTime() const override { return m_MaxDeltaTime; }

        void setUpdateDeltaTime(float dt) override;
        void setFixedDeltaTime(float dt) override;
        void setTimeScale(float scale) override;
        void setMaxFixedStepsPerFrame(uint32_t maxSteps) override;
        void setMaxDeltaTime(float dt) override;
        void setDeltaSmoothingFactor(float factor) override;

    protected:
        bool onInit() override;
        void onShutdown() override;
        void onPreUpdate(fsec dt) override;

    private:
        float m_UpdateDeltaTime {1.0f / 60.0f};
        float m_UnscaledDeltaTime {1.0f / 60.0f};
        float m_SmoothedDeltaTime {1.0f / 60.0f};
        float m_FixedDeltaTime {1.0f / 60.0f};

        float m_TotalTime {0.0f};
        float m_UnscaledTotalTime {0.0f};
        float m_AverageFrameTime {1.0f / 60.0f};
        float m_FramesPerSecond {60.0f};

        float m_TimeScale {1.0f};
        float m_MaxDeltaTime {0.25f};
        float m_DeltaSmoothingFactor {0.1f};
        float m_FixedAccumulator {0.0f};
        float m_FixedAlpha {0.0f};
        float m_FpsAccumulatedTime {0.0f};
        uint32_t m_FpsAccumulatedFrames {0};

        uint32_t m_FixedStepsThisFrame {0};
        uint32_t m_MaxFixedStepsPerFrame {8};
        uint64_t m_FrameIndex {0};
    };
} // namespace vultra
