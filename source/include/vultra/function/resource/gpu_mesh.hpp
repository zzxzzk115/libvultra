#pragma once

#include "vultra/core/rhi/index_buffer.hpp"
#include "vultra/core/rhi/vertex_attributes.hpp"
#include "vultra/core/rhi/vertex_buffer.hpp"

#include <cstdint>

namespace vultra::resource
{
    struct GpuVertexLayout
    {
        uint32_t              stride = 0;
        rhi::VertexAttributes attributes;
    };

    // GPU-only mesh representation. No CPU-side VMesh / SubMesh is stored here.
    // This is intentionally renderer-agnostic and will later evolve into GPU-driven
    // draw-indirect / meshlet dispatch buffers.
    struct GpuMesh
    {
        rhi::VertexAttributes vertexAttributes;

        rhi::VertexBuffer vertexBuffer;
        rhi::IndexBuffer  indexBuffer;

        // Buffer device addresses for GPU-driven vertex pulling.
        // Filled by AssetSystem at upload time using rhi::RenderDevice.
        uint64_t vertexBufferAddress {0};
        uint64_t indexBufferAddress {0};

        GpuVertexLayout layout;

        uint32_t vertexCount {0};
        uint32_t indexCount {0};

        // Offset/count into the global material table (GpuResourcePool).
        uint32_t materialOffset {0};
        uint32_t materialCount {0};
    };
} // namespace vultra::resource
