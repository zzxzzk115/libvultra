#pragma once

#include "vultra/core/base/script_annotations.hpp"
#include <vbase/service/service_registry.hpp>

#include <cstdint>

namespace vultra
{
    // Lua namespace `Time` (doc/lua_api_design.md), generated from these
    // VBIND_FN annotations via the IR pipeline. The wrappers null-check
    // ScriptContext::timingService before dispatching.
    class VBIND_MODULE(name = Time, area = timing, service = timingService) ITimingService
    {
    public:
        SERVICE_REGISTER(ITimingService)

        VBIND_FN(name = deltaTime) virtual float updateDeltaTime() const = 0;
        VBIND_FN() virtual float fixedDeltaTime() const                  = 0;
        VBIND_FN() virtual float unscaledDeltaTime() const               = 0;
        VBIND_FN() virtual float smoothedDeltaTime() const               = 0;

        VBIND_FN() virtual float totalTime() const         = 0;
        VBIND_FN() virtual float unscaledTotalTime() const = 0;
        VBIND_FN() virtual float averageFrameTime() const  = 0;
        VBIND_FN() virtual float framesPerSecond() const   = 0;

        VBIND_FN(null = 1.0f) virtual float timeScale() const    = 0;
        VBIND_FN() virtual float fixedAlpha() const              = 0;
        VBIND_FN() virtual uint32_t fixedStepsThisFrame() const  = 0;
        VBIND_FN() virtual uint64_t frameIndex() const           = 0;
        VBIND_FN() virtual float maxDeltaTime() const            = 0;

        virtual void setUpdateDeltaTime(float dt)                          = 0;
        VBIND_FN() virtual void setFixedDeltaTime(float dt)                = 0;
        VBIND_FN() virtual void setTimeScale(float scale)                  = 0;
        VBIND_FN() virtual void setMaxFixedStepsPerFrame(uint32_t maxSteps) = 0;
        VBIND_FN() virtual void setMaxDeltaTime(float dt)                  = 0;
        VBIND_FN() virtual void setDeltaSmoothingFactor(float factor)      = 0;
    };
} // namespace vultra
