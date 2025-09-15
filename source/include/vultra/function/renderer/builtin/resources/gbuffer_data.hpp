#pragma once

#include <fg/Fwd.hpp>

namespace vultra
{
    struct GBufferData
    {
        FrameGraphResource albedo;
        FrameGraphResource normal;
        FrameGraphResource emissive;
        FrameGraphResource metallicRoughnessAO;
        FrameGraphResource depth;
    };
} // namespace vultra