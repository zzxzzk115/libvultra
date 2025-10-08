#pragma once

#include "vultra/core/base/base.hpp"
#include <vultra/function/renderer/mesh_resource.hpp>

#include <glm/mat4x4.hpp>

namespace vultra
{
    namespace gfx
    {
        struct Renderable
        {
            Ref<gfx::DefaultMesh> mesh {nullptr};
            glm::mat4             modelMatrix {1.0f};
        };

        struct RenderableGroup
        {
            std::vector<Renderable> renderables;

            struct GPUInstanceData
            {
                uint32_t geometryOffset;
                uint32_t geometryCount;
                uint32_t materialOffset;
                uint32_t materialCount;
            };

            std::vector<GPUInstanceData> instances;     // One per renderable
            std::vector<GPUGeometryNode> geometryNodes; // Global list of geometry nodes
            std::vector<GPUMaterial>     materials;     // Global list of materials

            rhi::AccelerationStructure tlas; // For raytracing purposes

            Ref<rhi::StorageBuffer> instanceBuffer {nullptr};
            Ref<rhi::StorageBuffer> geometryNodeBuffer {nullptr};
            Ref<rhi::StorageBuffer> materialBuffer {nullptr};

            void build(rhi::RenderDevice& rd)
            {
                std::vector<rhi::RayTracingInstance> tlasInstances;
                tlasInstances.reserve(renderables.size());

                for (uint32_t i = 0; i < renderables.size(); ++i)
                {
                    const auto& renderable = renderables[i];
                    if (renderable.mesh && renderable.mesh->renderMesh.blas)
                    {
                        rhi::RayTracingInstance instance {};
                        instance.blas            = &renderable.mesh->renderMesh.blas;
                        instance.transform       = renderable.modelMatrix;
                        instance.instanceID      = i; // Use index as instance ID
                        instance.mask            = 0xFF;
                        instance.sbtRecordOffset = 0;
                        instance.flags           = vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable;

                        tlasInstances.push_back(instance);
                    }
                }

                if (!tlasInstances.empty())
                {
                    tlas = rd.createBuildMultipleInstanceTLAS(tlasInstances);
                }

                // Build instances, global geometry node and material lists
                instances.clear();
                geometryNodes.clear();
                materials.clear();

                for (const auto& renderable : renderables)
                {
                    GPUInstanceData instanceData {};
                    instanceData.geometryOffset = static_cast<uint32_t>(geometryNodes.size());
                    instanceData.geometryCount  = static_cast<uint32_t>(renderable.mesh->renderMesh.subMeshes.size());
                    instanceData.materialOffset = static_cast<uint32_t>(materials.size());
                    instanceData.materialCount  = static_cast<uint32_t>(renderable.mesh->materials.size());
                    instances.push_back(instanceData);

                    // Append geometry nodes
                    for (const auto& sm : renderable.mesh->renderMesh.subMeshes)
                    {
                        GPUGeometryNode node {};
                        node.vertexBufferAddress = sm.vertexBufferAddress;
                        node.indexBufferAddress  = sm.indexBufferAddress;
                        node.materialIndex       = sm.materialIndex;
                        geometryNodes.push_back(node);
                    }

                    // Append materials
                    for (const auto& mat : renderable.mesh->materials)
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
                        gpuMat.alphaCutoff            = mat.alphaCutoff;
                        gpuMat.alphaMode              = static_cast<int>(mat.alphaMode);
                        gpuMat.doubleSided            = mat.doubleSided ? 1 : 0;
                        materials.push_back(gpuMat);
                    }
                }

                // Create and upload buffers

                // Instance Buffer
                if (!instances.empty())
                {
                    instanceBuffer = createRef<rhi::StorageBuffer>(
                        std::move(rd.createStorageBuffer(sizeof(GPUInstanceData) * instances.size())));

                    auto stagingInstanceBuffer =
                        rd.createStagingBuffer(sizeof(GPUInstanceData) * instances.size(), instances.data());

                    rd.execute(
                        [&](rhi::CommandBuffer& cb) {
                            cb.copyBuffer(stagingInstanceBuffer,
                                          *instanceBuffer,
                                          vk::BufferCopy {0, 0, stagingInstanceBuffer.getSize()});
                        },
                        true);
                }
                else
                {
                    instanceBuffer = nullptr;
                }

                // Geometry Node Buffer
                if (!geometryNodes.empty())
                {
                    geometryNodeBuffer = createRef<rhi::StorageBuffer>(
                        std::move(rd.createStorageBuffer(sizeof(GPUGeometryNode) * geometryNodes.size())));

                    auto stagingGeometryNodeBuffer =
                        rd.createStagingBuffer(sizeof(GPUGeometryNode) * geometryNodes.size(), geometryNodes.data());

                    rd.execute(
                        [&](rhi::CommandBuffer& cb) {
                            cb.copyBuffer(stagingGeometryNodeBuffer,
                                          *geometryNodeBuffer,
                                          vk::BufferCopy {0, 0, stagingGeometryNodeBuffer.getSize()});
                        },
                        true);
                }
                else
                {
                    geometryNodeBuffer = nullptr;
                }

                // Material Buffer
                if (!materials.empty())
                {
                    materialBuffer = createRef<rhi::StorageBuffer>(
                        std::move(rd.createStorageBuffer(sizeof(GPUMaterial) * materials.size())));

                    auto stagingMaterialBuffer =
                        rd.createStagingBuffer(sizeof(GPUMaterial) * materials.size(), materials.data());

                    rd.execute(
                        [&](rhi::CommandBuffer& cb) {
                            cb.copyBuffer(stagingMaterialBuffer,
                                          *materialBuffer,
                                          vk::BufferCopy {0, 0, stagingMaterialBuffer.getSize()});
                        },
                        true);
                }
                else
                {
                    materialBuffer = nullptr;
                }
            }

            void clear()
            {
                renderables.clear();
                tlas = {};
            }

            bool empty() const { return renderables.empty(); }
        };

        struct RenderPrimitive
        {
            Ref<gfx::DefaultMesh> mesh {nullptr};
            glm::mat4             modelMatrix {1.0f};
            gfx::SubMesh          renderSubMesh;
            uint32_t              renderSubMeshIndex {0};
        };

        struct RenderPrimitiveGroup
        {
            std::vector<RenderPrimitive> opaquePrimitives;
            std::vector<RenderPrimitive> alphaMaskingPrimitives;
            std::vector<RenderPrimitive> decalPrimitives;

            void clear()
            {
                opaquePrimitives.clear();
                alphaMaskingPrimitives.clear();
                decalPrimitives.clear();
            }

            bool empty() const
            {
                return opaquePrimitives.empty() && alphaMaskingPrimitives.empty() && decalPrimitives.empty();
            }
        };
    } // namespace gfx
} // namespace vultra