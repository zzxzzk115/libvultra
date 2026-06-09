#pragma once

#include <cstdint>

namespace vultra
{
    // Push constants describing the meshlet pool sizes for the meshlet draw passes (visibility-buffer and
    // thin-GBuffer). The layout is identical in both and must stay in lockstep with the shader-side
    // push-constant block, so it lives in one place instead of being redefined per pass.
    struct MeshletDrawPushConstants
    {
        uint32_t maxDraws {0};
        uint32_t maxMeshlets {0};
        uint32_t maxMeshletVertices {0};
        uint32_t maxMeshletTriangles {0};
    };
} // namespace vultra
