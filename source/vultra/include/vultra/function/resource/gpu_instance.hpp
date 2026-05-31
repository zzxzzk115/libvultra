#pragma once

#include <cstdint>

namespace vultra::resource
{
    // GPU-driven instance record.
    // Instances reference scene tables (meshes/materials/textures) by stable indices.
    //
    // This is intentionally minimal and can be expanded as the GPU-driven pipeline evolves:
    // - multiview: viewMask or viewIndex
    // - batching: batchId / drawGroup
    // - skinning: skinOffset
    // - ray tracing: blasIndex
    struct GpuInstance
    {
        uint32_t meshIndex {0};
        uint32_t materialIndex {0};
        uint32_t transformIndex {0};
        uint32_t flags {0};
        uint32_t entityPickingId {0};
        uint32_t skinMatrixOffset {0xFFFFFFFFu};
        uint32_t skinMatrixCount {0};
        uint32_t padding2 {0};
    };
} // namespace vultra::resource
