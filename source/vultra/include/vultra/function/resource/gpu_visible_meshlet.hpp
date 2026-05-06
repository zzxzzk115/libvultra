#pragma once

#include <cstdint>

namespace vultra::resource
{
    // Output of MeshletCull pass.
    // This is intentionally compact and stable so it can be consumed by both
    // CPU debugging paths and the later BuildIndirect compute path.
    struct GpuVisibleMeshlet
    {
        uint32_t meshletIndex {0};
        uint32_t instanceIndex {0};
        uint32_t materialIndex {0};
        uint32_t flags {0};
    };
} // namespace vultra::resource
