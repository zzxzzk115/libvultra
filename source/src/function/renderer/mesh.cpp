#include "vultra/function/renderer/mesh.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/index_buffer.hpp"
#include "vultra/core/rhi/vertex_attributes.hpp"

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
            // define a "degenerate" AABB;
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
            const glm::vec3 p = *reinterpret_cast<const glm::vec3*>(&positions[i]);
            mn                = glm::min(mn, p);
            mx                = glm::max(mx, p);
        }

        aabb.min = mn;
        aabb.max = mx;
        return aabb;
    }

    void Mesh::buildVertexAndIndexBuffers(rhi::RenderDevice& rd, const vasset::VMesh& asset)
    {
        assert(info.vertexFormat != nullptr);

        info.vertexStride = info.vertexFormat->getStride();

        const uint32_t vertexCount = info.vertexCount;

        // ---------------------------------------------------------
        // Interleave vertex data
        // ---------------------------------------------------------

        std::vector<uint8_t> interleaved;
        interleaved.resize(info.vertexStride * vertexCount);

        for (uint32_t i = 0; i < vertexCount; ++i)
        {
            uint8_t* dst = interleaved.data() + i * info.vertexStride;

            for (const auto& attr : info.vertexFormat->getAttributes())
            {
                const auto  semantic = static_cast<AttributeLocation>(attr.first);
                const auto& desc     = attr.second;

                uint8_t* writePtr = dst + desc.offset;

                switch (semantic)
                {
                    case AttributeLocation::ePosition:

                        memcpy(writePtr, &asset.positions[i], sizeof(glm::vec3));

                        break;

                    case AttributeLocation::eNormal:

                        memcpy(writePtr, &asset.normals[i], sizeof(glm::vec3));

                        break;

                    case AttributeLocation::eColor:

                        if (!asset.colors.empty())
                        {
                            memcpy(writePtr, &asset.colors[i],
                                   sizeof(glm::vec3)); // or vec4 depending
                        }
                        break;

                    case AttributeLocation::eTexCoord0:

                        if (!asset.texCoords0.empty())
                        {
                            memcpy(writePtr, &asset.texCoords0[i], sizeof(glm::vec2));
                        }
                        break;

                    case AttributeLocation::eTexCoord1:

                        if (!asset.texCoords1.empty())
                        {
                            memcpy(writePtr, &asset.texCoords1[i], sizeof(glm::vec2));
                        }
                        break;

                    case AttributeLocation::eTangent:

                        if (!asset.tangents.empty())
                        {
                            memcpy(writePtr, &asset.tangents[i], sizeof(glm::vec4));
                        }
                        break;

                    case AttributeLocation::eJoints:

                        if (!asset.jointIndices.empty())
                        {
                            memcpy(writePtr, &asset.jointIndices[i], sizeof(glm::vec4));
                        }
                        break;

                    case AttributeLocation::eWeights:

                        if (!asset.jointWeights.empty())
                        {
                            memcpy(writePtr, &asset.jointWeights[i], sizeof(glm::vec4));
                        }
                        break;

                    case AttributeLocation::eBitangent:
                        assert(false && "Bitangent semantic is not supported for now.");
                        break;

                    default:
                        assert(false && "Unsupported vertex attribute");
                        break;
                }

#ifndef NDEBUG
                // Safety check
                assert(rhi::getSize(desc.type) == (semantic == AttributeLocation::ePosition  ? sizeof(glm::vec3) :
                                                   semantic == AttributeLocation::eNormal    ? sizeof(glm::vec3) :
                                                   semantic == AttributeLocation::eColor     ? sizeof(glm::vec3) :
                                                   semantic == AttributeLocation::eTexCoord0 ? sizeof(glm::vec2) :
                                                   semantic == AttributeLocation::eTexCoord1 ? sizeof(glm::vec2) :
                                                   semantic == AttributeLocation::eTangent   ? sizeof(glm::vec4) :
                                                   semantic == AttributeLocation::eJoints    ? sizeof(glm::vec4) :
                                                   semantic == AttributeLocation::eWeights   ? sizeof(glm::vec4) :
                                                   semantic == AttributeLocation::eBitangent ? sizeof(glm::vec3) :
                                                                                               0));

#endif
            }
        }

        // ---------------------------------------------------------
        // Create GPU vertex buffer
        // ---------------------------------------------------------

        gpuBuffers.vertexBuffer =
            createRef<rhi::VertexBuffer>(std::move(rd.createVertexBuffer(info.vertexStride, vertexCount)));

        auto stagingVertexBuffer = rd.createStagingBuffer(interleaved.size(), interleaved.data());

        // ---------------------------------------------------------
        // Index buffer
        // ---------------------------------------------------------

        rhi::Buffer stagingIndexBuffer {};

        if (!asset.indices.empty())
        {
            gpuBuffers.indexBuffer = createRef<rhi::IndexBuffer>(
                std::move(rd.createIndexBuffer(rhi::IndexType::eUInt32, asset.indices.size())));

            stagingIndexBuffer = rd.createStagingBuffer(sizeof(uint32_t) * asset.indices.size(), asset.indices.data());
        }

        // ---------------------------------------------------------
        // Upload
        // ---------------------------------------------------------

        rd.execute(
            [&](rhi::CommandBuffer& cb) {
                cb.copyBuffer(stagingVertexBuffer,
                              *gpuBuffers.vertexBuffer,
                              vk::BufferCopy {0, 0, stagingVertexBuffer.getSize()});

                if (stagingIndexBuffer)
                {
                    cb.copyBuffer(stagingIndexBuffer,
                                  *gpuBuffers.indexBuffer,
                                  vk::BufferCopy {0, 0, stagingIndexBuffer.getSize()});
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
            // TODO: Load material from runtime asset registry using matRef's UUID info.
            GPUMaterial m {};
            // m.albedoIndex    = matRef.albedoIndex;
            // m.alphaMaskIndex = matRef.alphaMaskIndex;
            // m.metallicIndex  = matRef.metallicIndex;
            // m.roughnessIndex = matRef.roughnessIndex;

            // m.specularIndex = matRef.specularIndex;
            // m.normalIndex   = matRef.normalIndex;
            // m.aoIndex       = matRef.aoIndex;
            // m.emissiveIndex = matRef.emissiveIndex;

            // m.metallicRoughnessIndex = matRef.metallicRoughnessIndex;

            // m.baseColor              = matRef.baseColor;
            // m.emissiveColorIntensity = matRef.emissiveColorIntensity;
            // m.ambientColor           = matRef.ambientColor;

            // m.opacity         = matRef.opacity;
            // m.metallicFactor  = matRef.metallicFactor;
            // m.roughnessFactor = matRef.roughnessFactor;
            // m.ior             = matRef.ior;

            // m.alphaCutoff = matRef.alphaCutoff;
            // m.alphaMode   = static_cast<int>(matRef.alphaMode);
            // m.doubleSided = matRef.doubleSided ? 1 : 0;

            gpuMaterials.push_back(m);
        }

        if (gpuMaterials.empty())
        {
            gpuBuffers.materialBuffer = nullptr;
            return;
        }

        gpuBuffers.materialBuffer =
            createRef<rhi::StorageBuffer>(std::move(rd.createStorageBuffer(sizeof(GPUMaterial) * gpuMaterials.size())));
        createRef<rhi::StorageBuffer>(std::move(rd.createStorageBuffer(sizeof(GPUMaterial) * gpuMaterials.size())));

        auto staging = rd.createStagingBuffer(sizeof(GPUMaterial) * gpuMaterials.size(), gpuMaterials.data());

        rd.execute(
            [&](rhi::CommandBuffer& cb) {
                cb.copyBuffer(staging, *gpuBuffers.materialBuffer, vk::BufferCopy {0, 0, staging.getSize()});
            },
            true);
    }

    // -------------------------------------------------------------------------
    // SubMeshes + meshlet buffers per submesh
    // -------------------------------------------------------------------------
    void Mesh::buildSubMeshesAndMeshlets(rhi::RenderDevice& rd, const vasset::VMesh& asset)
    {
        subMeshes.clear();
        subMeshes.reserve(asset.subMeshes.size());

        for (const auto& sm : asset.subMeshes)
        {
            SubMesh out {};
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
    // -------------------------------------------------------------------------
    void Mesh::buildRenderMeshInternal(rhi::RenderDevice& rd)
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
                rsm.vertexBufferAddress = rd.getBufferDeviceAddress(*gpuBuffers.vertexBuffer) +
                                          static_cast<uint64_t>(sm.vertexOffset) * info.vertexStride;

                rsm.indexBufferAddress = gpuBuffers.indexBuffer ?
                                             (rd.getBufferDeviceAddress(*gpuBuffers.indexBuffer) +
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

            rsm.vertexStride = info.vertexStride;
            rsm.vertexCount  = sm.vertexCount;

            rsm.indexCount = sm.indexCount;
            rsm.indexType  = gpuBuffers.indexBuffer ? gpuBuffers.indexBuffer->getIndexType() : rhi::IndexType::eUInt32;

            rsm.materialIndex = sm.materialIndex;

            // TODO: get material alpha mode and set opaque flag accordingly. For now, we assume all meshes are opaque.
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
