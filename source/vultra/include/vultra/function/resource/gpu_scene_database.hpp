#pragma once

#include "vultra/core/base/base.hpp"
#include "vultra/core/rhi/acceleration_structure.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/rhi/storage_buffer.hpp"
#include "vultra/core/rhi/structs/device_address.hpp"
#include "vultra/core/rhi/structs/raytracing_instance.hpp"
#include "vultra/function/resource/gpu_instance.hpp"
#include "vultra/function/resource/gpu_mesh_table.hpp"
#include "vultra/function/resource/gpu_resource_pool.hpp"

#include <glm/mat4x4.hpp>

#include <glm/common.hpp>

#include <limits>

#include <cstdint>
#include <vector>

namespace vultra::rhi
{
    class CommandBuffer;
}

namespace vultra::resource
{
    struct GpuRayTracingInstance
    {
        uint32_t geometryOffset {0};
        uint32_t geometryCount {0};
        uint32_t materialOffset {0};
        uint32_t materialCount {0};
    };
    static_assert(sizeof(GpuRayTracingInstance) == 16);

    struct GpuRayTracingGeometryNode
    {
        rhi::DeviceAddress vertexBufferAddress {};
        rhi::DeviceAddress indexBufferAddress {};
        uint32_t           materialIndex {0};
        uint32_t           vertexStrideBytes {0};
    };
    static_assert(sizeof(GpuRayTracingGeometryNode) == 24);

    // Persistent-ish GPU scene database.
    //
    // Responsibilities:
    // - Stable pointer to the global GPU resource pool
    // - Scene tables uploaded for the current cooked world snapshot
    // - Shared database consumed by either CPU-driven or GPU-driven views
    //
    // Notes:
    // - We intentionally keep CPU staging mirrors even for GPU-driven buffers.
    //   They are useful for debugging, deterministic rebuilds when buffers grow,
    //   and as a fallback path while both CPU-driven and GPU-driven pipelines
    //   coexist during the migration.
    struct GpuSceneDatabase
    {
        const GpuResourcePool* resources {nullptr};

        // CPU staging / snapshot data
        std::vector<GpuInstance>       instances;
        std::vector<glm::mat4>         transforms;
        std::vector<GpuMeshTableEntry> meshTable;

        std::vector<GpuRayTracingInstance>     rayTracingInstances;
        std::vector<GpuRayTracingGeometryNode> rayTracingGeometryNodes;
        rhi::AccelerationStructure             rayTracingTlas;

        // GPU buffers
        Ref<rhi::StorageBuffer> instanceBuffer {nullptr};
        Ref<rhi::StorageBuffer> transformBuffer {nullptr};
        Ref<rhi::StorageBuffer> meshTableBuffer {nullptr};
        Ref<rhi::StorageBuffer> rayTracingInstanceBuffer {nullptr};
        Ref<rhi::StorageBuffer> rayTracingGeometryNodeBuffer {nullptr};

        void clear()
        {
            resources = nullptr;
            instances.clear();
            transforms.clear();
            meshTable.clear();
            rayTracingInstances.clear();
            rayTracingGeometryNodes.clear();
            rayTracingTlas = {};
            instanceBuffer  = nullptr;
            transformBuffer = nullptr;
            meshTableBuffer = nullptr;
            rayTracingInstanceBuffer    = nullptr;
            rayTracingGeometryNodeBuffer = nullptr;
        }

        void beginFrame(const GpuResourcePool& res)
        {
            resources = &res;
            instances.clear();
            transforms.clear();
            meshTable.clear();
            rayTracingInstances.clear();
            rayTracingGeometryNodes.clear();
            rayTracingTlas = {};
        }

        uint32_t pushTransform(const glm::mat4& model)
        {
            const uint32_t index = static_cast<uint32_t>(transforms.size());
            transforms.push_back(model);
            return index;
        }

        uint32_t pushInstance(const GpuInstance& inst)
        {
            const uint32_t index = static_cast<uint32_t>(instances.size());
            instances.push_back(inst);
            return index;
        }

        void rebuildMeshTableFromResources()
        {
            meshTable.clear();
            if (!resources)
                return;

            meshTable.reserve(resources->meshes.size());
            for (const auto& mesh : resources->meshes)
            {
                GpuMeshTableEntry e {};
                e.meshletOffset     = mesh.meshletOffset;
                e.meshletCount      = mesh.meshletCount;
                e.materialOffset    = mesh.materialOffset;
                e.materialCount     = mesh.materialCount;
                e.vertexStrideBytes = mesh.vertexStrideBytes;
                e.vertexByteOffset  = mesh.vertexByteOffset;
                e.indexBase         = mesh.indexBase;
                e.flags             = 0;

                // Build conservative mesh-space bounds from meshlet bounds.
                if (mesh.meshletCount > 0 &&
                    mesh.meshletOffset + mesh.meshletCount <= resources->meshlets.cpuMeshlets.size())
                {
                    glm::vec3 bmin(std::numeric_limits<float>::max());
                    glm::vec3 bmax(std::numeric_limits<float>::lowest());

                    for (uint32_t i = 0; i < mesh.meshletCount; ++i)
                    {
                        const auto& ml = resources->meshlets.cpuMeshlets[mesh.meshletOffset + i];
                        const glm::vec3 r(ml.radius);
                        bmin = glm::min(bmin, ml.center - r);
                        bmax = glm::max(bmax, ml.center + r);
                    }

                    const glm::vec3 center = (bmin + bmax) * 0.5f;
                    e.boundsCenter         = center;
                    e.boundsRadius         = glm::length(bmax - center);
                }

                meshTable.push_back(e);
            }
        }

        void ensureInstanceBuffer(rhi::RenderDevice& rd)
        {
            const size_t bytes = instances.size() * sizeof(GpuInstance);
            if (bytes == 0)
                return;
            if (!instanceBuffer || instanceBuffer->getSize() < bytes)
                instanceBuffer = createRef<rhi::StorageBuffer>(rd.createStorageBuffer(bytes));
        }

        void ensureTransformBuffer(rhi::RenderDevice& rd)
        {
            const size_t bytes = transforms.size() * sizeof(glm::mat4);
            if (bytes == 0)
                return;
            if (!transformBuffer || transformBuffer->getSize() < bytes)
                transformBuffer = createRef<rhi::StorageBuffer>(rd.createStorageBuffer(bytes));
        }

        void ensureMeshTableBuffer(rhi::RenderDevice& rd)
        {
            const size_t bytes = meshTable.size() * sizeof(GpuMeshTableEntry);
            if (bytes == 0)
                return;
            if (!meshTableBuffer || meshTableBuffer->getSize() < bytes)
                meshTableBuffer = createRef<rhi::StorageBuffer>(rd.createStorageBuffer(bytes));
        }

        void ensureRayTracingBuffers(rhi::RenderDevice& rd)
        {
            const size_t instanceBytes = rayTracingInstances.size() * sizeof(GpuRayTracingInstance);
            if (instanceBytes > 0 &&
                (!rayTracingInstanceBuffer || rayTracingInstanceBuffer->getSize() < instanceBytes))
                rayTracingInstanceBuffer = createRef<rhi::StorageBuffer>(rd.createStorageBuffer(instanceBytes));

            const size_t geometryBytes = rayTracingGeometryNodes.size() * sizeof(GpuRayTracingGeometryNode);
            if (geometryBytes > 0 &&
                (!rayTracingGeometryNodeBuffer || rayTracingGeometryNodeBuffer->getSize() < geometryBytes))
                rayTracingGeometryNodeBuffer = createRef<rhi::StorageBuffer>(rd.createStorageBuffer(geometryBytes));
        }

        void uploadRayTracingBuffers(rhi::RenderDevice& rd)
        {
            ensureRayTracingBuffers(rd);

            const size_t instanceBytes = rayTracingInstances.size() * sizeof(GpuRayTracingInstance);
            if (instanceBytes > 0)
                rd.uploadS(*rayTracingInstanceBuffer, 0, static_cast<uint64_t>(instanceBytes), rayTracingInstances.data());

            const size_t geometryBytes = rayTracingGeometryNodes.size() * sizeof(GpuRayTracingGeometryNode);
            if (geometryBytes > 0)
                rd.uploadS(*rayTracingGeometryNodeBuffer,
                           0,
                           static_cast<uint64_t>(geometryBytes),
                           rayTracingGeometryNodes.data());
        }

        void rebuildRayTracingScene(rhi::RenderDevice& rd)
        {
            rayTracingInstances.clear();
            rayTracingGeometryNodes.clear();
            rayTracingTlas = {};

            if (!resources)
                return;

            std::vector<rhi::RayTracingInstance> tlasInstances;
            tlasInstances.reserve(instances.size());

            for (uint32_t instanceIndex = 0; instanceIndex < static_cast<uint32_t>(instances.size()); ++instanceIndex)
            {
                const auto& inst = instances[instanceIndex];
                if (inst.meshIndex >= resources->meshes.size() || inst.transformIndex >= transforms.size())
                    continue;

                auto& mesh = const_cast<GpuMesh&>(resources->meshes[inst.meshIndex]);
                if (!mesh.blas || mesh.subMeshes.empty())
                    continue;

                GpuRayTracingInstance rtInst {};
                rtInst.geometryOffset = static_cast<uint32_t>(rayTracingGeometryNodes.size());
                rtInst.geometryCount  = static_cast<uint32_t>(mesh.subMeshes.size());
                rtInst.materialOffset = mesh.materialOffset;
                rtInst.materialCount  = mesh.materialCount;

                for (const auto& sm : mesh.subMeshes)
                {
                    GpuRayTracingGeometryNode node {};
                    node.vertexBufferAddress.value =
                        mesh.vertexBufferAddress ? mesh.vertexBufferAddress.value :
                                                   rd.getBufferDeviceAddress(mesh.vertexBuffer).value;
                    node.indexBufferAddress.value =
                        mesh.indexBufferAddress ? mesh.indexBufferAddress.value :
                                                  rd.getBufferDeviceAddress(mesh.indexBuffer).value;
                    node.indexBufferAddress.value += static_cast<uint64_t>(sm.indexOffset) * sizeof(uint32_t);
                    node.materialIndex      = sm.materialIndex >= mesh.materialOffset ? sm.materialIndex - mesh.materialOffset : sm.materialIndex;
                    node.vertexStrideBytes  = mesh.vertexStrideBytes;
                    rayTracingGeometryNodes.push_back(node);
                }

                const uint32_t rtInstanceIndex = static_cast<uint32_t>(rayTracingInstances.size());
                rayTracingInstances.push_back(rtInst);

                rhi::RayTracingInstance tlasInst {};
                tlasInst.blas       = &mesh.blas;
                tlasInst.transform  = transforms[inst.transformIndex];
                tlasInst.instanceID = rtInstanceIndex;
                tlasInstances.push_back(tlasInst);
            }

            uploadRayTracingBuffers(rd);

            if (!tlasInstances.empty())
                rayTracingTlas = rd.createBuildMultipleInstanceTLAS(tlasInstances);
        }

        void uploadInstances(rhi::RenderDevice& rd, rhi::CommandBuffer& cb)
        {
            ensureInstanceBuffer(rd);
            const size_t bytes = instances.size() * sizeof(GpuInstance);
            if (bytes > 0)
                cb.update(*instanceBuffer, 0, static_cast<uint64_t>(bytes), instances.data());
        }

        void uploadTransforms(rhi::RenderDevice& rd, rhi::CommandBuffer& cb)
        {
            ensureTransformBuffer(rd);
            const size_t bytes = transforms.size() * sizeof(glm::mat4);
            if (bytes > 0)
                cb.update(*transformBuffer, 0, static_cast<uint64_t>(bytes), transforms.data());
        }

        void uploadMeshTable(rhi::RenderDevice& rd, rhi::CommandBuffer& cb)
        {
            ensureMeshTableBuffer(rd);
            const size_t bytes = meshTable.size() * sizeof(GpuMeshTableEntry);
            if (bytes > 0)
                cb.update(*meshTableBuffer, 0, static_cast<uint64_t>(bytes), meshTable.data());
        }

        void uploadSceneTables(rhi::RenderDevice& rd, rhi::CommandBuffer& cb)
        {
            // Per-frame scene uploads must be recorded into the frame command buffer.
            // Avoid synchronous uploadS() here to prevent mid-frame submit/wait stalls.
            uploadInstances(rd, cb);
            uploadTransforms(rd, cb);
            uploadMeshTable(rd, cb);
        }
    };
} // namespace vultra::resource
