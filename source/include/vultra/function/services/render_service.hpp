#pragma once

#include "vultra/core/base/base.hpp"

#include <vbase/service/service_registry.hpp>

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
    };
} // namespace vultra
