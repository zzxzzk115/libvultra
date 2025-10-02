#pragma once

#include "vultra/core/base/base.hpp"
#include "vultra/core/math/aabb.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
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

        struct alignas(16) GPUMaterial
        {
            uint32_t albedoIndex {0};
            uint32_t alphaMaskIndex {0};
            uint32_t metallicIndex {0};
            uint32_t roughnessIndex {0};
            uint32_t specularIndex {0};
            uint32_t normalIndex {0};
            uint32_t aoIndex {0};
            uint32_t emissiveIndex {0};
            uint32_t metallicRoughnessIndex {0};

            // padding
            uint32_t padding0 {0};
            uint32_t padding1 {0};
            uint32_t padding2 {0};

            glm::vec4 baseColor {1.0f, 1.0f, 1.0f, 1.0f};
            glm::vec4 emissiveColorIntensity {0.0f, 0.0f, 0.0f, 1.0f};
            glm::vec4 ambientColor {0.0f, 0.0f, 0.0f, 1.0f};
            float     opacity {1.0f};
            float     metallicFactor {0.0f};
            float     roughnessFactor {0.0f};
            float     ior {1.0f};
            int       doubleSided {0};
        };
        static_assert(sizeof(GPUMaterial) % 16 == 0);

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

            Ref<rhi::VertexBuffer>  vertexBuffer {nullptr};
            Ref<rhi::IndexBuffer>   indexBuffer {nullptr};
            Ref<rhi::StorageBuffer> materialBuffer {nullptr};

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

            void buildMaterialBuffer(rhi::RenderDevice& rd)
            {
                // Create material buffer
                std::vector<GPUMaterial> gpuMaterials;
                gpuMaterials.reserve(materials.size());

                for (const auto& mat : materials)
                {
                    GPUMaterial gpuMat {};
                    gpuMat.albedoIndex            = mat.albedoIndex;
                    gpuMat.alphaMaskIndex         = mat.alphaMaskIndex;
                    gpuMat.metallicIndex          = mat.metallicIndex;
                    gpuMat.roughnessIndex         = mat.roughnessIndex;
                    gpuMat.specularIndex          = mat.specularIndex;
                    gpuMat.normalIndex            = mat.normalIndex;
                    gpuMat.aoIndex                = mat.aoIndex;
                    gpuMat.emissiveIndex          = mat.emissiveIndex;
                    gpuMat.metallicRoughnessIndex = mat.metallicRoughnessIndex;
                    gpuMat.baseColor              = mat.baseColor;
                    gpuMat.emissiveColorIntensity = mat.emissiveColorIntensity;
                    gpuMat.ambientColor           = mat.ambientColor;
                    gpuMat.opacity                = mat.opacity;
                    gpuMat.metallicFactor         = mat.metallicFactor;
                    gpuMat.roughnessFactor        = mat.roughnessFactor;
                    gpuMat.ior                    = mat.ior;
                    gpuMat.doubleSided            = mat.doubleSided ? 1 : 0;
                    gpuMaterials.push_back(gpuMat);
                }

                materialBuffer = createRef<rhi::StorageBuffer>(
                    std::move(rd.createStorageBuffer(sizeof(GPUMaterial) * gpuMaterials.size())));

                auto stagingBuffer =
                    rd.createStagingBuffer(sizeof(GPUMaterial) * gpuMaterials.size(), gpuMaterials.data());

                rd.execute(
                    [&](rhi::CommandBuffer& cb) {
                        cb.copyBuffer(stagingBuffer, *materialBuffer, vk::BufferCopy {0, 0, stagingBuffer.getSize()});
                    },
                    true);
            }

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

                // Create geometry node buffer (raytracing only)
                {
                    struct GPUGeometryNode
                    {
                        uint64_t vertexBufferAddress {0};
                        uint64_t indexBufferAddress {0};
                        uint32_t materialIndex {0};
                    };
                    std::vector<GPUGeometryNode> geometryNodes;
                    geometryNodes.reserve(renderMesh.subMeshes.size());

                    for (const auto& sm : renderMesh.subMeshes)
                    {
                        GPUGeometryNode node {};
                        node.vertexBufferAddress = sm.vertexBufferAddress;
                        node.indexBufferAddress  = sm.indexBufferAddress;
                        node.materialIndex       = sm.materialIndex;
                        geometryNodes.push_back(node);
                    }

                    renderMesh.geometryNodeBuffer = createRef<rhi::StorageBuffer>(
                        std::move(rd.createStorageBuffer(sizeof(GPUGeometryNode) * geometryNodes.size())));

                    auto stagingBuffer =
                        rd.createStagingBuffer(sizeof(GPUGeometryNode) * geometryNodes.size(), geometryNodes.data());

                    rd.execute(
                        [&](rhi::CommandBuffer& cb) {
                            cb.copyBuffer(stagingBuffer,
                                          *renderMesh.geometryNodeBuffer,
                                          vk::BufferCopy {0, 0, stagingBuffer.getSize()});
                        },
                        true);
                }
            }
        };
    } // namespace gfx
} // namespace vultra