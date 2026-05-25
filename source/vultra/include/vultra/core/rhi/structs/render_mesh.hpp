#pragma once

#include "vultra/core/base/base.hpp"
#include "vultra/core/rhi/acceleration_structure.hpp"
#include "vultra/core/rhi/storage_buffer.hpp"
#include "vultra/core/rhi/structs/device_address.hpp"
#include "vultra/core/rhi/structs/index_type.hpp"

#include <cstdint>
#include <vector>

namespace vultra
{
    namespace rhi
    {
        class RenderDevice;

        struct RenderSubMesh
        {
            DeviceAddress vertexBufferAddress;
            DeviceAddress indexBufferAddress;
            DeviceAddress transformBufferAddress;

            DeviceAddress meshletBufferAddress;
            DeviceAddress meshletVertexBufferAddress;
            DeviceAddress meshletTriangleBufferAddress;
            uint32_t      meshletCount {0};

            uint32_t vertexStride {0};
            uint32_t vertexCount {0};
            uint32_t vertexOffset {0};
            uint32_t positionOffsetBytes {0};

            uint32_t  indexCount {0};
            IndexType indexType {IndexType::eUInt32};

            uint32_t materialIndex {0};

            bool opaque {true};
        };

        struct RenderMesh
        {
            std::vector<RenderSubMesh> subMeshes;
            Ref<StorageBuffer>         geometryNodeBuffer {nullptr};
            AccelerationStructure      blas;

            void createBuildBLAS(RenderDevice& rd);
        };
    } // namespace rhi
} // namespace vultra
