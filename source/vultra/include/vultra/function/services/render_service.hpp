#pragma once

#include "vultra/core/base/base.hpp"

#include <vbase/service/service_registry.hpp>

#include <cstdint>

namespace vultra
{
    class Renderer;
    class RuntimeProfiler;
    class World;
    struct GaussianSplatFrameStats;
    struct GaussianSplatRenderSettings;
    struct BuiltinRenderSettings;
    struct RenderCamera;

    class IRenderService
    {
    public:
        SERVICE_REGISTER(IRenderService)

        virtual void registerRenderer(Ref<Renderer> renderer) = 0;

        // Render one frame for all cooked cameras (CameraSystem output).
        virtual void renderFrame() = 0;

        // Notify render service that output size changed.
        virtual void onResize(uint32_t width, uint32_t height) = 0;

        // Built-in runtime profiler (default disabled).
        virtual RuntimeProfiler* runtimeProfiler() = 0;

        virtual GaussianSplatRenderSettings&       gaussianSplatSettings() = 0;
        virtual const GaussianSplatRenderSettings& gaussianSplatSettings() const = 0;
        virtual const GaussianSplatFrameStats&     gaussianSplatFrameStats() const = 0;
        virtual BuiltinRenderSettings&             builtinRenderSettings() = 0;
        virtual const BuiltinRenderSettings&       builtinRenderSettings() const = 0;
    };
} // namespace vultra
