#pragma once

#include "vultra/core/rhi/index_buffer.hpp"
#include "vultra/core/rhi/raytracing/acceleration_structure.hpp"

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

            uint32_t vertexStride {0};
            uint32_t vertexCount {0};

            uint32_t  indexCount {0};
            IndexType indexType {IndexType::eUInt32};

            uint32_t materialIndex {0};
        };

        struct RenderMesh
        {
            std::vector<RenderSubMesh> subMeshes;

            AccelerationStructure blas;

            void createBuildBLAS(RenderDevice& rd);
        };
    } // namespace rhi
} // namespace vultra