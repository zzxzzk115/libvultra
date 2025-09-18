#pragma once

#include "vultra/core/base/base.hpp"
#include "vultra/core/math/aabb.hpp"
#include "vultra/core/rhi/index_buffer.hpp"
#include "vultra/core/rhi/primitive_topology.hpp"
#include "vultra/core/rhi/vertex_buffer.hpp"

#include <vector>

namespace vultra
{
    namespace gfx
    {
        class VertexFormat;

        template<typename MaterialType>
        struct SubMesh
        {
            rhi::PrimitiveTopology topology {rhi::PrimitiveTopology::eTriangleList};

            uint32_t vertexOffset {0};
            uint32_t vertexCount {0};

            uint32_t indexOffset {0};
            uint32_t indexCount {0};

            MaterialType material {};
        };

        template<typename VertexType, typename MaterialType>
        struct Mesh
        {
            std::vector<VertexType> vertices;
            std::vector<uint32_t>   indices;
            AABB                    aabb;

            std::vector<SubMesh<MaterialType>> subMeshes;

            Ref<rhi::VertexBuffer> vertexBuffer {nullptr};
            Ref<rhi::IndexBuffer>  indexBuffer {nullptr};

            Ref<VertexFormat> vertexFormat {nullptr};

            rhi::PrimitiveTopology topology {rhi::PrimitiveTopology::eTriangleList};

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

            inline void generateTangents()
            {
                std::vector<glm::vec3> bitangentAccum(vertices.size(), glm::vec3(0.0f));

                // Walk through all triangles directly using indices
                for (size_t i = 0; i < indices.size(); i += 3)
                {
                    uint32_t i0 = indices[i + 0];
                    uint32_t i1 = indices[i + 1];
                    uint32_t i2 = indices[i + 2];

                    auto& v0 = vertices[i0];
                    auto& v1 = vertices[i1];
                    auto& v2 = vertices[i2];

                    const glm::vec3 deltaPos1 = v1.position - v0.position;
                    const glm::vec3 deltaPos2 = v2.position - v0.position;

                    const glm::vec2 deltaUV1 = v1.texCoord - v0.texCoord;
                    const glm::vec2 deltaUV2 = v2.texCoord - v0.texCoord;

                    float denom = deltaUV1.x * deltaUV2.y - deltaUV1.y * deltaUV2.x;
                    if (fabs(denom) < 1e-8f) // Degenerate UV triangle → skip
                        continue;

                    const float     r = 1.0f / denom;
                    const glm::vec3 t = (deltaPos1 * deltaUV2.y - deltaPos2 * deltaUV1.y) * r;
                    const glm::vec3 b = (deltaPos2 * deltaUV1.x - deltaPos1 * deltaUV2.x) * r;

                    v0.tangent += glm::vec4(t, 0.0f);
                    v1.tangent += glm::vec4(t, 0.0f);
                    v2.tangent += glm::vec4(t, 0.0f);

                    bitangentAccum[i0] += b;
                    bitangentAccum[i1] += b;
                    bitangentAccum[i2] += b;
                }

                // Normalize and compute handedness
                for (size_t i = 0; i < vertices.size(); i++)
                {
                    auto& v = vertices[i];

                    glm::vec3 n = glm::normalize(v.normal);
                    glm::vec3 t = glm::normalize(glm::vec3(v.tangent));
                    t           = glm::normalize(t - n * glm::dot(n, t)); // Gram–Schmidt

                    glm::vec3 b = glm::normalize(bitangentAccum[i]);
                    float     w = (glm::dot(glm::cross(n, t), b) < 0.0f) ? -1.0f : 1.0f;

                    v.tangent = glm::vec4(t, w);
                }
            }
        };
    } // namespace gfx
} // namespace vultra