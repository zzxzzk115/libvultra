#pragma once

#include "vultra/core/base/base.hpp"
#include "vultra/core/rhi/index_buffer.hpp"
#include "vultra/core/rhi/raytracing/acceleration_structure.hpp"
#include "vultra/core/rhi/storage_buffer.hpp"

#include <glm/fwd.hpp>

#include <cstdint>

namespace vultra
{
    namespace rhi
    {
        struct RenderSubMesh
        {
            uint64_t vertexBufferAddress {0};
            uint64_t indexBufferAddress {0};
            uint64_t transformBufferAddress {0}; // Optional

            uint64_t meshletBufferAddress {0};
            uint64_t meshletVertexBufferAddress {0};
            uint64_t meshletTriangleBufferAddress {0};
            uint32_t meshletCount {0};

            uint32_t vertexStride {0};
            uint32_t vertexCount {0};

            uint32_t  indexCount {0};
            IndexType indexType {IndexType::eUInt32};

            uint32_t materialIndex {0};

            bool opaque {true};
        };

        struct RenderMesh
        {
            std::vector<RenderSubMesh> subMeshes;

            Ref<StorageBuffer> geometryNodeBuffer {nullptr};

            AccelerationStructure blas;

            void createBuildBLAS(RenderDevice& rd);
        };
    } // namespace rhi
} // namespace vultra