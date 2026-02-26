#pragma once

#include <cstdint>

#include <glm/mat4x4.hpp>

namespace vultra::resource
{
    // GPU-driven per-draw record.
    //
    // Consumed by GPU-driven shaders for vertex pulling and per-draw state lookup.
    //
    // Notes:
    // - vertexAddress / indexAddress are buffer device addresses (uint64_t) produced by rhi::RenderDevice.
    // - index buffer is uint32 indices.
    // - For non-indexed indirect drawing, indexCount/firstIndex map to DrawIndirectCommand.count/first
    //   and the vertex shader treats gl_VertexIndex as the index-buffer element index.
    //
    // Layout: std430 friendly (16-byte aligned).
    struct GpuDrawRecord
    {
        uint64_t vertexAddress {0};
        uint64_t indexAddress {0};

        glm::mat4 model {1.0f};

        uint32_t materialIndex {0};

        // Index range in the index buffer (uint32 indices).
        uint32_t firstIndex {0};
        uint32_t indexCount {0};

        uint32_t flags {0};
    };

    static_assert(sizeof(GpuDrawRecord) % 16 == 0, "GpuDrawRecord must be 16-byte aligned");
} // namespace vultra::resource
