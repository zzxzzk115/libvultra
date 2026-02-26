#pragma once

#include <vbase/service/service_registry.hpp>

#include <vector>

namespace vultra
{
    struct RenderCamera;

    class ICameraService
    {
    public:
        SERVICE_REGISTER(ICameraService);

        // Access cooked cameras for the current frame.
        virtual std::vector<RenderCamera> cameras() = 0;
    };
} // namespace vultra
