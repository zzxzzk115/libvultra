#include "vultra/function/renderer/mesh.hpp"
#include "vultra/core/rhi/alpha_mode.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/primitive_topology.hpp"

#include <cassert>
#include <cstring>
#include <limits>
#include <utility>

namespace vultra::gfx
{
    // -------------------------------------------------------------------------
    // Create
    // -------------------------------------------------------------------------
    Ref<Mesh> Mesh::create(rhi::RenderDevice& rd, const vasset::VMesh& asset)
    {
        auto mesh = createRef<Mesh>();
        mesh->buildFromAsset(rd, asset);
        return mesh;
    }

    void Mesh::rebuildRenderMesh(rhi::RenderDevice& rd) { buildRenderMeshInternal(rd); }

    void Mesh::buildFromAsset(rhi::RenderDevice& rd, const vasset::VMesh& asset)
    {
        info.uuid = asset.uuid;

        info.vertexCount = asset.vertexCount;
        info.indexCount  = static_cast<uint32_t>(asset.indices.size());

        buildVertexFormat(asset.vertexFlags);
        buildVertexAndIndexBuffers(rd, asset);
        buildMaterialBuffer(rd, asset);
        buildSubMeshesAndMeshlets(rd, asset);
        buildRenderMeshInternal(rd);
    }

    void Mesh::buildVertexFormat(vasset::VVertexFlags flags)
    {
        auto builder = VertexFormat::Builder();

        uint32_t offset = 0;

        builder.setAttribute(AttributeLocation::ePosition,
                             rhi::VertexAttribute {rhi::VertexAttribute::Type::eFloat3, offset});

        if (HasFlagValues(flags, vasset::VVertexFlags::eNormal))
        {
            offset += sizeof(vasset::VNormal);
            builder.setAttribute(AttributeLocation::eNormal,
                                 rhi::VertexAttribute {rhi::VertexAttribute::Type::eFloat3, offset});
        }

        if (HasFlagValues(flags, vasset::VVertexFlags::eColor))
        {
            offset += sizeof(vasset::VColor);
            builder.setAttribute(AttributeLocation::eColor,
                                 rhi::VertexAttribute {rhi::VertexAttribute::Type::eFloat3, offset});
        }

        if (HasFlagValues(flags, vasset::VVertexFlags::eTexCoord0))
        {
            offset += sizeof(vasset::VTexCoord);
            builder.setAttribute(AttributeLocation::eTexCoord0,
                                 rhi::VertexAttribute {rhi::VertexAttribute::Type::eFloat2, offset});
        }

        if (HasFlagValues(flags, vasset::VVertexFlags::eTexCoord1))
        {
            offset += sizeof(vasset::VTexCoord);
            builder.setAttribute(AttributeLocation::eTexCoord1,
                                 rhi::VertexAttribute {rhi::VertexAttribute::Type::eFloat2, offset});
        }

        if (HasFlagValues(flags, vasset::VVertexFlags::eTangent))
        {
            offset += sizeof(vasset::VTangent);
            builder.setAttribute(AttributeLocation::eTangent,
                                 rhi::VertexAttribute {rhi::VertexAttribute::Type::eFloat4, offset});
        }

        if (HasFlagValues(flags, vasset::VVertexFlags::eJointIndices))
        {
            offset += sizeof(vasset::VJointIndices);
            builder.setAttribute(AttributeLocation::eJoints,
                                 rhi::VertexAttribute {rhi::VertexAttribute::Type::eFloat4, offset});
        }

        if (HasFlagValues(flags, vasset::VVertexFlags::eJointWeights))
        {
            offset += sizeof(vasset::VJointWeights);
            builder.setAttribute(AttributeLocation::eWeights,
                                 rhi::VertexAttribute {rhi::VertexAttribute::Type::eFloat4, offset});
        }

        info.vertexFormat = builder.build();
    }

    AABB Mesh::computeAABBForRange(const std::vector<vasset::VPosition>& positions,
                                      uint32_t                              vertexOffset,
                                      uint32_t                              vertexCount)
    {
        AABB aabb {};
        if (positions.empty() || vertexCount == 0)
        {
            // define a "degenerate" AABB; adjust if your AABB has explicit invalid state
            aabb.min = glm::vec3(0.0f);
            aabb.max = glm::vec3(0.0f);
            return aabb;
        }

        const uint32_t end = vertexOffset + vertexCount;
        const uint32_t n   = static_cast<uint32_t>(positions.size());

        const uint32_t beginClamped = (vertexOffset < n) ? vertexOffset : n;
        const uint32_t endClamped   = (end < n) ? end : n;

        if (beginClamped >= endClamped)
        {
            aabb.min = glm::vec3(0.0f);
            aabb.max = glm::vec3(0.0f);
            return aabb;
        }

        glm::vec3 mn(std::numeric_limits<float>::max());
        glm::vec3 mx(std::numeric_limits<float>::lowest());

        for (uint32_t i = beginClamped; i < endClamped; ++i)
        {
            // NOTE: adapt if VPosition is not glm::vec3 compatible
            const glm::vec3 p = *reinterpret_cast<const glm::vec3*>(&positions[i]);
            mn                = glm::min(mn, p);
            mx                = glm::max(mx, p);
        }

        aabb.min = mn;
        aabb.max = mx;
        return aabb;
    }

    // -------------------------------------------------------------------------
    // Buffer creation adapters — adapt to your RenderDevice API
    // -------------------------------------------------------------------------
    Ref<rhi::VertexBuffer> Mesh::createVertexBufferRaw(rhi::RenderDevice& rd, size_t sizeBytes)
    {
        // If you have: rd.createVertexBuffer(sizeBytes) -> rhi::VertexBuffer
        // replace this whole function with that.
        //
        // Fallback: create a StorageBuffer then wrap it as VertexBuffer if your engine allows.
        // If not possible, switch to your real API here.

        auto vb = rd.createVertexBuffer(static_cast<uint64_t>(sizeBytes)); // <-- adjust if signature differs
        return createRef<rhi::VertexBuffer>(std::move(vb));
    }

    Ref<rhi::IndexBuffer> Mesh::createIndexBufferRaw(rhi::RenderDevice& rd, size_t sizeBytes)
    {
        // If you have: rd.createIndexBuffer(sizeBytes, IndexType::eUInt32) -> rhi::IndexBuffer
        // replace this whole function with that.

        auto ib =
            rd.createIndexBuffer(static_cast<uint64_t>(sizeBytes), rhi::IndexType::eUInt32); // <-- adjust if needed
        return createRef<rhi::IndexBuffer>(std::move(ib));
    }

    // -------------------------------------------------------------------------
    // Build vertex/index buffers
    // -------------------------------------------------------------------------
    void Mesh::buildVertexAndIndexBuffers(rhi::RenderDevice& rd, const vasset::VMesh& asset)
    {
        info.vertexStride = computePackedVertexStride(asset.vertexFlags);

        std::vector<std::byte> vertexBytes;
        packVerticesInterleaved(asset, vertexBytes, vertexStride);

        // Create GPU buffers
        vertexBuffer = createVertexBufferRaw(rd, vertexBytes.size());
        if (!vertexBuffer)
        {
            // You can replace this with your Result<> pipeline if preferred
            assert(false && "Failed to create vertex buffer");
            return;
        }

        if (!asset.indices.empty())
        {
            const size_t indexBytes = asset.indices.size() * sizeof(uint32_t);
            indexBuffer             = createIndexBufferRaw(rd, indexBytes);
            if (!indexBuffer)
            {
                assert(false && "Failed to create index buffer");
                return;
            }
        }
        else
        {
            indexBuffer = nullptr;
        }

        // Upload with staging buffers
        auto stagingVB = rd.createStagingBuffer(vertexBytes.size(), vertexBytes.data());

        Ref<rhi::StagingBuffer> stagingIB = nullptr;
        if (indexBuffer && !asset.indices.empty())
        {
            stagingIB = createRef<rhi::StagingBuffer>(
                std::move(rd.createStagingBuffer(asset.indices.size() * sizeof(uint32_t), asset.indices.data())));
        }

        rd.execute(
            [&](rhi::CommandBuffer& cb) {
                cb.copyBuffer(stagingVB, *vertexBuffer, vk::BufferCopy {0, 0, stagingVB.getSize()});

                if (stagingIB && indexBuffer)
                {
                    cb.copyBuffer(*stagingIB, *indexBuffer, vk::BufferCopy {0, 0, stagingIB->getSize()});
                }
            },
            true);
    }

    // -------------------------------------------------------------------------
    // Material buffer
    // -------------------------------------------------------------------------
    void Mesh::buildMaterialBuffer(rhi::RenderDevice& rd, const vasset::VMesh& asset)
    {
        std::vector<GPUMaterial> gpuMaterials;
        gpuMaterials.reserve(asset.materials.size());

        for (const auto& matRef : asset.materials)
        {
            // TODO: Embedding material & 
            GPUMaterial m {};
            m.albedoIndex    = matRef.albedoIndex;
            m.alphaMaskIndex = matRef.alphaMaskIndex;
            m.metallicIndex  = matRef.metallicIndex;
            m.roughnessIndex = matRef.roughnessIndex;

            m.specularIndex = matRef.specularIndex;
            m.normalIndex   = matRef.normalIndex;
            m.aoIndex       = matRef.aoIndex;
            m.emissiveIndex = matRef.emissiveIndex;

            m.metallicRoughnessIndex = matRef.metallicRoughnessIndex;

            m.baseColor              = matRef.baseColor;
            m.emissiveColorIntensity = matRef.emissiveColorIntensity;
            m.ambientColor           = matRef.ambientColor;

            m.opacity         = matRef.opacity;
            m.metallicFactor  = matRef.metallicFactor;
            m.roughnessFactor = matRef.roughnessFactor;
            m.ior             = matRef.ior;

            m.alphaCutoff = matRef.alphaCutoff;
            m.alphaMode   = static_cast<int>(matRef.alphaMode);
            m.doubleSided = matRef.doubleSided ? 1 : 0;

            gpuMaterials.push_back(m);
        }

        if (gpuMaterials.empty())
        {
            materialBuffer = nullptr;
            return;
        }

        materialBuffer =
            createRef<rhi::StorageBuffer>(std::move(rd.createStorageBuffer(sizeof(GPUMaterial) * gpuMaterials.size())));

        auto staging = rd.createStagingBuffer(sizeof(GPUMaterial) * gpuMaterials.size(), gpuMaterials.data());

        rd.execute(
            [&](rhi::CommandBuffer& cb) {
                cb.copyBuffer(staging, *materialBuffer, vk::BufferCopy {0, 0, staging.getSize()});
            },
            true);
    }

    // -------------------------------------------------------------------------
    // SubMeshes + meshlet buffers per submesh
    // -------------------------------------------------------------------------
    void GPUMesh::buildSubMeshesAndMeshlets(rhi::RenderDevice& rd, const vasset::VMesh& asset)
    {
        subMeshes.clear();
        subMeshes.reserve(asset.subMeshes.size());

        for (const auto& sm : asset.subMeshes)
        {
            GPUSubMesh out {};
            out.name          = sm.name;
            out.vertexOffset  = sm.vertexOffset;
            out.vertexCount   = sm.vertexCount;
            out.indexOffset   = sm.indexOffset;
            out.indexCount    = sm.indexCount;
            out.materialIndex = sm.materialIndex;
            out.aabb          = computeAABBForRange(asset.positions, sm.vertexOffset, sm.vertexCount);

            // Meshlets: upload if present
            const auto& mg            = sm.meshletGroup;
            out.meshlets.meshletCount = static_cast<uint32_t>(mg.meshlets.size());

            if (!mg.meshlets.empty())
            {
                out.meshlets.meshletBuffer = createRef<rhi::StorageBuffer>(
                    std::move(rd.createStorageBuffer(sizeof(GPUMeshlet) * mg.meshlets.size())));

                auto staging = rd.createStagingBuffer(sizeof(GPUMeshlet) * mg.meshlets.size(), mg.meshlets.data());

                rd.execute(
                    [&](rhi::CommandBuffer& cb) {
                        cb.copyBuffer(staging, *out.meshlets.meshletBuffer, vk::BufferCopy {0, 0, staging.getSize()});
                    },
                    true);
            }

            if (!mg.meshletVertices.empty())
            {
                out.meshlets.meshletVertexBuffer = createRef<rhi::StorageBuffer>(
                    std::move(rd.createStorageBuffer(sizeof(uint32_t) * mg.meshletVertices.size())));

                auto staging =
                    rd.createStagingBuffer(sizeof(uint32_t) * mg.meshletVertices.size(), mg.meshletVertices.data());

                rd.execute(
                    [&](rhi::CommandBuffer& cb) {
                        cb.copyBuffer(
                            staging, *out.meshlets.meshletVertexBuffer, vk::BufferCopy {0, 0, staging.getSize()});
                    },
                    true);
            }

            if (!mg.meshletTriangles.empty())
            {
                out.meshlets.meshletTriangleBuffer = createRef<rhi::StorageBuffer>(
                    std::move(rd.createStorageBuffer(sizeof(uint8_t) * mg.meshletTriangles.size())));

                auto staging =
                    rd.createStagingBuffer(sizeof(uint8_t) * mg.meshletTriangles.size(), mg.meshletTriangles.data());

                rd.execute(
                    [&](rhi::CommandBuffer& cb) {
                        cb.copyBuffer(
                            staging, *out.meshlets.meshletTriangleBuffer, vk::BufferCopy {0, 0, staging.getSize()});
                    },
                    true);
            }

            subMeshes.push_back(std::move(out));
        }
    }

    // -------------------------------------------------------------------------
    // RenderMesh build (ray tracing / mesh shader / BDA)
    // Mirrors your previous buildRenderMesh() but works off GPUMesh.
    // -------------------------------------------------------------------------
    void GPUMesh::buildRenderMeshInternal(rhi::RenderDevice& rd)
    {
        const auto& features = rd.getFeatureFlag();

        renderMesh.subMeshes.clear();
        renderMesh.subMeshes.reserve(subMeshes.size());

        for (const auto& sm : subMeshes)
        {
            rhi::RenderSubMesh rsm {};

            assert(vertexBuffer != nullptr);

            const bool wantBDA = HasFlagValues(features, rhi::RenderDeviceFeatureFlagBits::eRayTracingPipeline) ||
                                 HasFlagValues(features, rhi::RenderDeviceFeatureFlagBits::eRayQuery) ||
                                 HasFlagValues(features, rhi::RenderDeviceFeatureFlagBits::eMeshShader);

            if (wantBDA)
            {
                rsm.vertexBufferAddress =
                    rd.getBufferDeviceAddress(*vertexBuffer) + static_cast<uint64_t>(sm.vertexOffset) * vertexStride;

                rsm.indexBufferAddress = indexBuffer ? (rd.getBufferDeviceAddress(*indexBuffer) +
                                                        static_cast<uint64_t>(sm.indexOffset) * sizeof(uint32_t)) :
                                                       0;

                rsm.transformBufferAddress = 0; // Optional
            }

            // Meshlet buffers (mesh shader path)
            if (HasFlagValues(features, rhi::RenderDeviceFeatureFlagBits::eMeshShader))
            {
                rsm.meshletBufferAddress =
                    sm.meshlets.meshletBuffer ? rd.getBufferDeviceAddress(*sm.meshlets.meshletBuffer) : 0;

                rsm.meshletVertexBufferAddress =
                    sm.meshlets.meshletVertexBuffer ? rd.getBufferDeviceAddress(*sm.meshlets.meshletVertexBuffer) : 0;

                rsm.meshletTriangleBufferAddress = sm.meshlets.meshletTriangleBuffer ?
                                                       rd.getBufferDeviceAddress(*sm.meshlets.meshletTriangleBuffer) :
                                                       0;

                rsm.meshletCount = sm.meshlets.meshletCount;
            }

            rsm.vertexStride = vertexStride;
            rsm.vertexCount  = sm.vertexCount;

            rsm.indexCount = sm.indexCount;
            rsm.indexType  = indexBuffer ? indexBuffer->getIndexType() : rhi::IndexType::eUInt32;

            rsm.materialIndex = sm.materialIndex;

            // Opaque flag: if you want this in RenderSubMesh, map your alpha mode here.
            // If you don’t have resolved materials yet, default to opaque.
            rsm.opaque = true;

            renderMesh.subMeshes.push_back(rsm);
        }

        const bool wantRT = HasFlagValues(features, rhi::RenderDeviceFeatureFlagBits::eRayTracingPipeline) ||
                            HasFlagValues(features, rhi::RenderDeviceFeatureFlagBits::eRayQuery);

        if (wantRT)
        {
            renderMesh.createBuildBLAS(rd);

            // geometry node buffer (ray tracing only)
            std::vector<GPUGeometryNode> geometryNodes;
            geometryNodes.reserve(renderMesh.subMeshes.size());

            for (const auto& sm : renderMesh.subMeshes)
            {
                GPUGeometryNode n {};
                n.vertexBufferAddress = sm.vertexBufferAddress;
                n.indexBufferAddress  = sm.indexBufferAddress;
                n.materialIndex       = sm.materialIndex;
                geometryNodes.push_back(n);
            }

            renderMesh.geometryNodeBuffer = createRef<rhi::StorageBuffer>(
                std::move(rd.createStorageBuffer(sizeof(GPUGeometryNode) * geometryNodes.size())));

            auto staging = rd.createStagingBuffer(sizeof(GPUGeometryNode) * geometryNodes.size(), geometryNodes.data());

            rd.execute(
                [&](rhi::CommandBuffer& cb) {
                    cb.copyBuffer(staging, *renderMesh.geometryNodeBuffer, vk::BufferCopy {0, 0, staging.getSize()});
                },
                true);
        }
    }

} // namespace vultra::gfx
