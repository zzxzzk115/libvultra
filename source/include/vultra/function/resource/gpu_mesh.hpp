#pragma once

#include "vultra/core/rhi/index_buffer.hpp"
#include "vultra/core/rhi/vertex_attributes.hpp"
#include "vultra/core/rhi/vertex_buffer.hpp"

#include <cstdint>

namespace vultra::resource
{
    // GPU-only mesh representation. No CPU-side VMesh / SubMesh is stored here.
    // This is intentionally renderer-agnostic and will later evolve into GPU-driven
    // draw-indirect / meshlet dispatch buffers.
    struct GpuMesh
    {
        // Vertex layout (single source of truth for both CPU & GPU driven paths)
        rhi::VertexAttributes vertexAttributes;
        uint32_t              vertexStrideBytes {0};

        // CPU-driven buffers (optional)
        rhi::VertexBuffer vertexBuffer;
        rhi::IndexBuffer  indexBuffer;

        // Buffer device addresses for GPU-driven vertex pulling.
        // Filled by AssetSystem at upload time using rhi::RenderDevice.
        uint64_t vertexBufferAddress {0};
        uint64_t indexBufferAddress {0};

        // Range in the global geometry index buffer (GpuResourcePool::geometry).
        // Used by indexed multi-draw indirect.
        uint32_t indexBase {0}; // firstIndex

        // Range in the global vertex byte buffer (GpuResourcePool::geometry).
        // Used by vertex pulling.
        uint32_t vertexByteOffset {0};

        uint32_t vertexCount {0};
        uint32_t indexCount {0};

        // Offset/count into the global material table (GpuResourcePool).
        uint32_t materialOffset {0};
        uint32_t materialCount {0};
    };
} // namespace vultra::resource
