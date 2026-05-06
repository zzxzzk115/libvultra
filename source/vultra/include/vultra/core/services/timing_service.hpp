#pragma once

#include <vbase/service/service_registry.hpp>

#include <cstdint>

namespace vultra
{
    class ITimingService
    {
    public:
        SERVICE_REGISTER(ITimingService)

        virtual float updateDeltaTime() const = 0;
        virtual float fixedDeltaTime() const = 0;
        virtual float unscaledDeltaTime() const = 0;
        virtual float smoothedDeltaTime() const = 0;

        virtual float totalTime() const = 0;
        virtual float unscaledTotalTime() const = 0;
        virtual float averageFrameTime() const = 0;
        virtual float framesPerSecond() const = 0;

        virtual float timeScale() const = 0;
        virtual float fixedAlpha() const = 0;
        virtual uint32_t fixedStepsThisFrame() const = 0;
        virtual uint64_t frameIndex() const = 0;
        virtual float maxDeltaTime() const = 0;

        virtual void setUpdateDeltaTime(float dt) = 0;
        virtual void setFixedDeltaTime(float dt) = 0;
        virtual void setTimeScale(float scale) = 0;
        virtual void setMaxFixedStepsPerFrame(uint32_t maxSteps) = 0;
        virtual void setMaxDeltaTime(float dt) = 0;
        virtual void setDeltaSmoothingFactor(float factor) = 0;
    };
} // namespace vultra
