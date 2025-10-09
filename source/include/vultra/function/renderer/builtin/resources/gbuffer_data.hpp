#pragma once

#include <fg/Fwd.hpp>

namespace vultra
{
    namespace gfx
    {
        struct GBufferData
        {
            FrameGraphResource albedo;
            FrameGraphResource normal;
            FrameGraphResource emissive;
            FrameGraphResource metallicRoughnessAO;
            FrameGraphResource depth;
            FrameGraphResource textureLodDebug;
        };
    } // namespace gfx
} // namespace vultra