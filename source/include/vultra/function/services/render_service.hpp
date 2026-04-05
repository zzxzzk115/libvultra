#pragma once

#include "vultra/core/base/base.hpp"

#include <vbase/service/service_registry.hpp>

#include <cstdint>

namespace vultra
{
    class Renderer;
    class World;
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
    };
} // namespace vultra
