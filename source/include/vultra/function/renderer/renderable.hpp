#pragma once

#include "vultra/core/base/base.hpp"
#include <vultra/function/renderer/mesh_resource.hpp>

#include <glm/mat4x4.hpp>

#include <meshoptimizer.h>

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

            // -------- Ray Tracing Data --------

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

            void buildRayTracing(rhi::RenderDevice& rd)
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

            // -------- Meshlet data --------

            MeshletGroup              globalMeshletGroup; // Global meshlet group
            std::vector<SimpleVertex> globalVertices;     // Global vertex list
            std::vector<uint32_t>     globalIndices;      // Global index list

            Ref<rhi::StorageBuffer> globalMeshletBuffer {nullptr};
            Ref<rhi::StorageBuffer> globalMeshletVertexBuffer {nullptr};
            Ref<rhi::StorageBuffer> globalMeshletTriangleBuffer {nullptr};
            Ref<rhi::VertexBuffer>  globalVertexBuffer {nullptr};

            void buildMeshShading(rhi::RenderDevice& rd)
            {
                // Collect global vertices
                globalVertices.clear();
                globalIndices.clear();

                for (const auto& renderable : renderables)
                {
                    // Offset value
                    uint32_t vertexOffset = static_cast<uint32_t>(globalVertices.size());

                    // Precalculate positions & normals by applying model matrix
                    glm::mat4 normalMatrix = glm::transpose(glm::inverse(renderable.modelMatrix));
                    for (const auto& vertex : renderable.mesh->vertices)
                    {
                        SimpleVertex vtx  = vertex;
                        glm::vec4    pos  = renderable.modelMatrix * glm::vec4(vtx.position, 1.0f);
                        glm::vec4    norm = normalMatrix * glm::vec4(vtx.normal, 0.0f);
                        vtx.position      = glm::vec3(pos) / pos.w;
                        vtx.normal        = glm::normalize(glm::vec3(norm));
                        globalVertices.push_back(vtx);
                    }

                    // Collect indices
                    for (const auto& index : renderable.mesh->indices)
                    {
                        globalIndices.push_back(vertexOffset + index);
                    }
                }

                // Build global meshlet group
                // Collect positions for meshoptimizer
                std::vector<glm::vec3> positions;
                positions.reserve(globalVertices.size());
                for (const auto& v : globalVertices)
                {
                    positions.push_back(v.position);
                }

                // Extra: meshlets
                // https://github.com/zeux/meshoptimizer/tree/v0.24#clusterization
                globalMeshletGroup.meshlets.clear();
                globalMeshletGroup.meshletTriangles.clear();
                globalMeshletGroup.meshletVertices.clear();

                const size_t maxVerts = 64;
                const size_t maxTris  = 124;

                // allocate space for meshlet vertices and triangles
                size_t maxMeshlets = meshopt_buildMeshletsBound(globalIndices.size(), maxVerts, maxTris);
                std::vector<meshopt_Meshlet> meshletsTemp(maxMeshlets);

                globalMeshletGroup.meshletVertices.resize(maxMeshlets * maxVerts);
                globalMeshletGroup.meshletTriangles.resize(maxMeshlets * maxTris * 3);

                // build meshlets
                size_t meshletCount = meshopt_buildMeshlets(meshletsTemp.data(),
                                                            globalMeshletGroup.meshletVertices.data(),
                                                            globalMeshletGroup.meshletTriangles.data(),
                                                            globalIndices.data(),
                                                            globalIndices.size(),
                                                            reinterpret_cast<const float*>(positions.data()),
                                                            positions.size(),
                                                            sizeof(glm::vec3),
                                                            maxVerts,
                                                            maxTris,
                                                            0.0f);

                meshletsTemp.resize(meshletCount);

                // copy to Meshlet
                for (size_t i = 0; i < meshletCount; ++i)
                {
                    const auto& src = meshletsTemp[i];
                    Meshlet     dst {};

                    dst.vertexOffset   = src.vertex_offset;
                    dst.triangleOffset = src.triangle_offset;
                    dst.vertexCount    = src.vertex_count;
                    dst.triangleCount  = src.triangle_count;

                    // TODO: material index?

                    auto bounds = meshopt_computeMeshletBounds(&globalMeshletGroup.meshletVertices[dst.vertexOffset],
                                                               &globalMeshletGroup.meshletTriangles[dst.triangleOffset],
                                                               dst.triangleCount,
                                                               reinterpret_cast<const float*>(positions.data()),
                                                               positions.size(),
                                                               sizeof(glm::vec3));

                    dst.center = glm::vec3(bounds.center[0], bounds.center[1], bounds.center[2]);
                    dst.radius = bounds.radius;

                    globalMeshletGroup.meshlets.push_back(dst);
                }

                // Resize to actual used size
                const auto& last = globalMeshletGroup.meshlets.back();

                globalMeshletGroup.meshletVertices.resize(last.vertexOffset + last.vertexCount);
                globalMeshletGroup.meshletTriangles.resize(last.triangleOffset + ((last.triangleCount * 3 + 3) & ~3));

                // Storage Buffers
                globalMeshletBuffer = createRef<rhi::StorageBuffer>(
                    std::move(rd.createStorageBuffer(sizeof(Meshlet) * globalMeshletGroup.meshlets.size())));
                globalMeshletVertexBuffer = createRef<rhi::StorageBuffer>(
                    std::move(rd.createStorageBuffer(sizeof(uint32_t) * globalMeshletGroup.meshletVertices.size())));
                globalMeshletTriangleBuffer = createRef<rhi::StorageBuffer>(
                    std::move(rd.createStorageBuffer(sizeof(uint8_t) * globalMeshletGroup.meshletTriangles.size())));
                globalVertexBuffer = createRef<rhi::VertexBuffer>(
                    std::move(rd.createVertexBuffer(sizeof(SimpleVertex), globalVertices.size())));

                // Staging buffers
                auto stagingMeshletBuffer = rd.createStagingBuffer(sizeof(Meshlet) * globalMeshletGroup.meshlets.size(),
                                                                   globalMeshletGroup.meshlets.data());
                auto stagingMeshletVertexBuffer =
                    rd.createStagingBuffer(sizeof(uint32_t) * globalMeshletGroup.meshletVertices.size(),
                                           globalMeshletGroup.meshletVertices.data());
                auto stagingMeshletTriangleBuffer =
                    rd.createStagingBuffer(sizeof(uint8_t) * globalMeshletGroup.meshletTriangles.size(),
                                           globalMeshletGroup.meshletTriangles.data());
                auto stagingVertexBuffer =
                    rd.createStagingBuffer(sizeof(SimpleVertex) * globalVertices.size(), globalVertices.data());

                // Copy to GPU
                rd.execute(
                    [&](rhi::CommandBuffer& cb) {
                        cb.copyBuffer(stagingMeshletBuffer,
                                      *globalMeshletBuffer,
                                      vk::BufferCopy {0, 0, stagingMeshletBuffer.getSize()});
                        cb.copyBuffer(stagingMeshletVertexBuffer,
                                      *globalMeshletVertexBuffer,
                                      vk::BufferCopy {0, 0, stagingMeshletVertexBuffer.getSize()});
                        cb.copyBuffer(stagingMeshletTriangleBuffer,
                                      *globalMeshletTriangleBuffer,
                                      vk::BufferCopy {0, 0, stagingMeshletTriangleBuffer.getSize()});
                        cb.copyBuffer(stagingVertexBuffer,
                                      *globalVertexBuffer,
                                      vk::BufferCopy {0, 0, stagingVertexBuffer.getSize()});
                    },
                    true);
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