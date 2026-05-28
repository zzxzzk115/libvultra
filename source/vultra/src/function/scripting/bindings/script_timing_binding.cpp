#include "vultra/function/scripting/bindings/script_timing_binding.hpp"

#include "vultra/core/services/timing_service.hpp"
#include "vultra/function/scripting/bindings/script_binding_common.hpp"

namespace vultra
{
    void registerScriptTimingBindings(sol::state& lua, ScriptContext& ctx)
    {
        auto time = script_binding::getOrCreateTable(lua, "Time");

        time.set_function("deltaTime",
                          [&ctx]() { return ctx.timingService ? ctx.timingService->updateDeltaTime() : 0.0f; });
        time.set_function("fixedDeltaTime",
                          [&ctx]() { return ctx.timingService ? ctx.timingService->fixedDeltaTime() : 0.0f; });
        time.set_function("unscaledDeltaTime",
                          [&ctx]() { return ctx.timingService ? ctx.timingService->unscaledDeltaTime() : 0.0f; });
        time.set_function("smoothedDeltaTime",
                          [&ctx]() { return ctx.timingService ? ctx.timingService->smoothedDeltaTime() : 0.0f; });
        time.set_function("totalTime", [&ctx]() { return ctx.timingService ? ctx.timingService->totalTime() : 0.0f; });
        time.set_function("unscaledTotalTime",
                          [&ctx]() { return ctx.timingService ? ctx.timingService->unscaledTotalTime() : 0.0f; });
        time.set_function("averageFrameTime",
                          [&ctx]() { return ctx.timingService ? ctx.timingService->averageFrameTime() : 0.0f; });
        time.set_function("framesPerSecond",
                          [&ctx]() { return ctx.timingService ? ctx.timingService->framesPerSecond() : 0.0f; });
        time.set_function("timeScale", [&ctx]() { return ctx.timingService ? ctx.timingService->timeScale() : 1.0f; });
        time.set_function("fixedAlpha",
                          [&ctx]() { return ctx.timingService ? ctx.timingService->fixedAlpha() : 0.0f; });
        time.set_function("fixedStepsThisFrame",
                          [&ctx]() { return ctx.timingService ? ctx.timingService->fixedStepsThisFrame() : 0u; });
        time.set_function("frameIndex",
                          [&ctx]() { return ctx.timingService ? ctx.timingService->frameIndex() : 0ull; });
        time.set_function("maxDeltaTime",
                          [&ctx]() { return ctx.timingService ? ctx.timingService->maxDeltaTime() : 0.0f; });

        time.set_function("setTimeScale", [&ctx](float scale) {
            if (ctx.timingService)
                ctx.timingService->setTimeScale(scale);
        });
        time.set_function("setFixedDeltaTime", [&ctx](float dt) {
            if (ctx.timingService)
                ctx.timingService->setFixedDeltaTime(dt);
        });
        time.set_function("setMaxDeltaTime", [&ctx](float dt) {
            if (ctx.timingService)
                ctx.timingService->setMaxDeltaTime(dt);
        });
        time.set_function("setMaxFixedStepsPerFrame", [&ctx](uint32_t maxSteps) {
            if (ctx.timingService)
                ctx.timingService->setMaxFixedStepsPerFrame(maxSteps);
        });
        time.set_function("setDeltaSmoothingFactor", [&ctx](float factor) {
            if (ctx.timingService)
                ctx.timingService->setDeltaSmoothingFactor(factor);
        });
    }
} // namespace vultra
