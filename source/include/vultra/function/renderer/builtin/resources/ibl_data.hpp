#pragma once

#include <fg/FrameGraphResource.hpp>

namespace vultra
{
    namespace gfx
    {
        struct IBLData
        {
            FrameGraphResource brdfLUT;
            FrameGraphResource irradianceMap;
            FrameGraphResource prefilteredEnvMap;
        };
    } // namespace gfx
} // namespace vultra