#pragma once

#include "vultra/core/base/base.hpp"
#include "vultra/core/math/aabb.hpp"
#include "vultra/core/rhi/index_buffer.hpp"
#include "vultra/core/rhi/primitive_topology.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/rhi/render_mesh.hpp"
#include "vultra/core/rhi/vertex_buffer.hpp"

#include <vector>

namespace vultra
{
    namespace gfx
    {
        class VertexFormat;

        struct SubMesh
        {
            std::string name;

            rhi::PrimitiveTopology topology {rhi::PrimitiveTopology::eTriangleList};

            uint32_t vertexOffset {0};
            uint32_t vertexCount {0};

            uint32_t indexOffset {0};
            uint32_t indexCount {0};

            uint32_t materialIndex {0};
        };

        template<typename VertexType, typename MaterialType>
        struct Mesh
        {
            std::vector<VertexType> vertices;
            std::vector<uint32_t>   indices;
            AABB                    aabb;

            std::vector<SubMesh>      subMeshes;
            std::vector<MaterialType> materials;

            Ref<rhi::VertexBuffer> vertexBuffer {nullptr};
            Ref<rhi::IndexBuffer>  indexBuffer {nullptr};

            Ref<VertexFormat> vertexFormat {nullptr};

            rhi::PrimitiveTopology topology {rhi::PrimitiveTopology::eTriangleList};

            rhi::RenderMesh renderMesh {}; // Currently only used for ray tracing

            struct Light
            {
                std::vector<VertexType> vertices;
                glm::vec4               colorIntensity {1.0f, 1.0f, 1.0f, 1.0f};
            };
            std::vector<Light> lights;

            [[nodiscard]] auto& getVertices() { return vertices; }
            [[nodiscard]] auto& getIndices() { return indices; }
            [[nodiscard]] auto& getSubMeshes() { return subMeshes; }
            [[nodiscard]] auto& getVertexBuffer() { return vertexBuffer; }
            [[nodiscard]] auto& getIndexBuffer() { return indexBuffer; }
            [[nodiscard]] auto& getVertexFormat() { return vertexFormat; }
            [[nodiscard]] auto& getTopology() { return topology; }

            [[nodiscard]] auto        getVertexCount() const { return static_cast<uint32_t>(vertices.size()); }
            [[nodiscard]] auto        getIndexCount() const { return static_cast<uint32_t>(indices.size()); }
            [[nodiscard]] static auto getVertexStride() { return static_cast<uint32_t>(sizeof(VertexType)); }
            [[nodiscard]] static auto getIndexStride() { return sizeof(uint32_t); }

            void buildRenderMesh(rhi::RenderDevice& rd)
            {
                renderMesh.subMeshes.clear();
                renderMesh.subMeshes.reserve(subMeshes.size());

                for (const auto& sm : subMeshes)
                {
                    rhi::RenderSubMesh rsm {};
                    assert(vertexBuffer != nullptr);
                    rsm.vertexBufferAddress =
                        rd.getBufferDeviceAddress(*vertexBuffer) + sm.vertexOffset * getVertexStride();
                    rsm.indexBufferAddress =
                        indexBuffer ? (rd.getBufferDeviceAddress(*indexBuffer) + sm.indexOffset * getIndexStride()) : 0;
                    rsm.transformBufferAddress = 0; // Optional

                    rsm.vertexStride = getVertexStride();
                    rsm.vertexCount  = sm.vertexCount;

                    rsm.indexCount = sm.indexCount;
                    rsm.indexType  = indexBuffer ? indexBuffer->getIndexType() : rhi::IndexType::eUInt32;

                    rsm.materialIndex = sm.materialIndex;

                    renderMesh.subMeshes.push_back(rsm);
                }

                renderMesh.createBuildBLAS(rd);
            }
        };
    } // namespace gfx
} // namespace vultra