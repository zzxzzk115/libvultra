#pragma once

#include <glm/glm.hpp>

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

        uint32_t vertexAttributeMask {0};
        uint32_t positionOffsetBytes {0xFFFFFFFFu};
        uint32_t normalOffsetBytes {0xFFFFFFFFu};
        uint32_t colorOffsetBytes {0xFFFFFFFFu};
        uint32_t texCoord0OffsetBytes {0xFFFFFFFFu};
        uint32_t texCoord1OffsetBytes {0xFFFFFFFFu};
        uint32_t tangentOffsetBytes {0xFFFFFFFFu};
        uint32_t jointIndicesOffsetBytes {0xFFFFFFFFu};
        uint32_t jointWeightsOffsetBytes {0xFFFFFFFFu};
        uint32_t padding0 {0};
        uint32_t padding1 {0};
        uint32_t padding2 {0};

        // Mesh-space bounds used by coarse culling in meshlet passes.
        glm::vec3 boundsCenter {0.0f};
        float     boundsRadius {0.0f};
    };
} // namespace vultra::resource
