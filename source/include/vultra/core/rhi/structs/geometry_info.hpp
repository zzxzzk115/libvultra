#pragma once

#include "vultra/core/rhi/structs/vertex_attributes.hpp"
#include "vultra/core/rhi/structs/primitive_topology.hpp"

namespace vultra
{
    namespace rhi
    {
        class VertexBuffer;
        class IndexBuffer;

        struct GeometryInfo
        {
            PrimitiveTopology topology {PrimitiveTopology::eTriangleList};
            const VertexBuffer* vertexBuffer {nullptr};
            uint32_t            vertexOffset {0};
            uint32_t            numVertices {0};
            const IndexBuffer*  indexBuffer {nullptr};
            uint32_t            indexOffset {0};
            uint32_t            numIndices {0};
        };
    } // namespace rhi
} // namespace vultra
