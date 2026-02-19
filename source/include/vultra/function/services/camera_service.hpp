#pragma once

#include <vbase/service/service_registry.hpp>

#include <span>

namespace vultra
{
    struct RenderCamera;

    class ICameraService
    {
    public:
        SERVICE_REGISTER(ICameraService);

        // Access cooked cameras for the current frame.
        virtual std::span<RenderCamera> cameras() = 0;
    };
} // namespace vultra
