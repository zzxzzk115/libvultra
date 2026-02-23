#pragma once

#include "vultra/core/rhi/index_buffer.hpp"
#include "vultra/core/rhi/storage_buffer.hpp"
#include "vultra/core/rhi/vertex_buffer.hpp"

#include <cstdint>

namespace vultra::resource
{
    // GPU-only mesh representation. No CPU-side VMesh / SubMesh is stored here.
    // This is intentionally renderer-agnostic and will later evolve into GPU-driven
    // draw-indirect / meshlet dispatch buffers.
    struct GpuMesh
    {
        rhi::VertexBuffer vertexBuffer;
        rhi::IndexBuffer  indexBuffer;

        // Optional: a GPU buffer that stores draw ranges / material indices.
        // At this stage we keep it as a plain storage buffer for the future GPU-driven pipeline.
        rhi::StorageBuffer drawDataBuffer;

        uint32_t vertexCount {0};
        uint32_t indexCount {0};

        // Offset/count into a scene material array (GpuScene), so instances can reference materials by index.
        uint32_t materialOffset {0};
        uint32_t materialCount {0};
    };
} // namespace vultra::resource
