#pragma once

#include "vultra/core/rhi/index_buffer.hpp"
#include "vultra/core/rhi/vertex_attributes.hpp"
#include "vultra/core/rhi/vertex_buffer.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace vultra::resource
{
    struct alignas(16) GpuMeshlet
    {
        uint32_t vertexOffset {0};
        uint32_t vertexCount {0};
        uint32_t triangleOffset {0};
        uint32_t triangleCount {0};

        uint32_t materialIndex {0};
        uint32_t paddingU0 {0};
        uint32_t paddingU1 {0};
        uint32_t paddingU2 {0};

        glm::vec3 center {0.0f};
        float     radius {0.0f};

        glm::vec3 coneAxis {0.0f};
        float     coneCutoff {0.0f};

        glm::vec3 coneApex {0.0f};
        float     paddingF0 {0.0f};
    };
    static_assert(sizeof(GpuMeshlet) % 16 == 0, "GpuMeshlet must be 16-byte aligned");

    struct GpuSubMesh
    {
        uint32_t vertexOffset {0};
        uint32_t vertexCount {0};
        uint32_t indexOffset {0};
        uint32_t indexCount {0};
        uint32_t materialIndex {0};
    };

    // GPU-only mesh representation. No CPU-side VMesh / SubMesh is stored here.
    struct GpuMesh
    {
        rhi::VertexAttributes vertexAttributes;
        uint32_t              vertexStrideBytes {0};

        rhi::VertexBuffer vertexBuffer;
        rhi::IndexBuffer  indexBuffer;

        uint64_t vertexBufferAddress {0};
        uint64_t indexBufferAddress {0};

        uint32_t indexBase {0};
        uint32_t vertexByteOffset {0};

        uint32_t vertexCount {0};
        uint32_t indexCount {0};

        uint32_t materialOffset {0};
        uint32_t materialCount {0};

        std::vector<GpuSubMesh> subMeshes;

        // Global meshlet-table range inside GpuResourcePool::meshlets.
        uint32_t meshletOffset {0};
        uint32_t meshletCount {0};
    };
} // namespace vultra::resource
