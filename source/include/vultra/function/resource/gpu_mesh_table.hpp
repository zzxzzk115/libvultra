#pragma once

#include <cstdint>

namespace vultra::resource
{
    // Compact GPU-side mesh table entry used by compute passes.
    // This is a shader-facing view derived from GpuResourcePool::meshes.
    struct GpuMeshTableEntry
    {
        uint32_t meshletOffset {0};
        uint32_t meshletCount {0};
        uint32_t materialOffset {0};
        uint32_t materialCount {0};

        uint32_t vertexStrideBytes {0};
        uint32_t vertexByteOffset {0};
        uint32_t indexBase {0};
        uint32_t flags {0};
    };
} // namespace vultra::resource
