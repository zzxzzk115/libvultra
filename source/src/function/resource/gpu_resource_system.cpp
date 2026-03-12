#include "vultra/function/resource/gpu_resource_system.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/engine/engine_context.hpp"

#include <limits>
#include <vector>

namespace vultra
{
    bool GpuResourceSystem::onInit()
    {
        VULTRA_CORE_INFO("[GpuResourceSystem] Initializing...");

        VULTRA_CORE_TRACE("[GpuResourceSystem] Providing IGpuResourceService");
        ctx().services.provide<IGpuResourceService>(this);

        VULTRA_CORE_INFO("[GpuResourceSystem] Initialized!");
        return true;
    }

    void GpuResourceSystem::onShutdown()
    {
        VULTRA_CORE_INFO("[GpuResourceSystem] Shutting down");
        m_Pool.clear();
    }

    uint32_t GpuResourceSystem::createTexture(rhi::RenderDevice& rd, resource::GpuTexture tex)
    {
        m_Pool.ensureBindlessSlot0(rd);
        return m_Pool.addTexture(std::move(tex));
    }

    uint32_t GpuResourceSystem::createMesh(rhi::RenderDevice& rd, const GpuMeshCreateDesc& desc)
    {
        if (!desc.vertexData || desc.vertexDataBytes == 0 || desc.vertexCount == 0 || desc.vertexStrideBytes == 0)
            return std::numeric_limits<uint32_t>::max();

        resource::GpuMesh mesh;
        mesh.vertexAttributes  = desc.vertexAttributes;
        mesh.vertexStrideBytes = desc.vertexStrideBytes;
        mesh.vertexCount       = desc.vertexCount;
        mesh.indexCount        = desc.indexCount;

        const bool wantCpu = (desc.usage & GpuMeshUsageFlags::eCpuDriven) != GpuMeshUsageFlags::eNone;
        const bool wantGpu = (desc.usage & GpuMeshUsageFlags::eGpuDriven) != GpuMeshUsageFlags::eNone;

        if (wantCpu)
        {
            mesh.vertexBuffer =
                rd.createVertexBuffer(static_cast<rhi::Buffer::Stride>(desc.vertexStrideBytes), desc.vertexCount);
            rd.uploadS(mesh.vertexBuffer, 0, static_cast<uint64_t>(desc.vertexDataBytes), desc.vertexData);

            if (desc.indexCount > 0 && desc.indexData)
            {
                mesh.indexBuffer       = rd.createIndexBuffer(desc.indexType, desc.indexCount);
                const uint64_t ibBytes = static_cast<uint64_t>(desc.indexCount) *
                                         static_cast<uint64_t>(static_cast<uint32_t>(desc.indexType));
                rd.uploadS(mesh.indexBuffer, 0, ibBytes, desc.indexData);
            }
        }

        if (wantGpu)
        {
            const auto vAlloc        = m_Pool.geometry.appendVertexBytes(rd, desc.vertexDataBytes, desc.vertexData);
            mesh.vertexByteOffset    = vAlloc.byteOffset;
            mesh.vertexBufferAddress = m_Pool.geometry.vertexBytesAddress;

            if (desc.indexCount > 0 && desc.indexData)
            {
                if (desc.indexType != rhi::IndexType::eUInt32)
                {
                    VULTRA_CORE_ERROR("GpuResourceSystem::createMesh: global index pool only supports UInt32");
                }
                else
                {
                    mesh.indexBase = m_Pool.geometry.appendIndices(
                        rd, reinterpret_cast<const uint32_t*>(desc.indexData), desc.indexCount);
                    mesh.indexBufferAddress = m_Pool.geometry.index32Address;
                }
            }

            if (desc.meshletCount > 0 && desc.meshletData)
            {
                const uint32_t globalMeshletVertexBase =
                    (desc.meshletVertexCount > 0 && desc.meshletVertexData)
                        ? m_Pool.meshlets.appendMeshletVertices(rd, desc.meshletVertexData, desc.meshletVertexCount)
                        : static_cast<uint32_t>(m_Pool.meshlets.cpuMeshletVertices.size());

                const uint32_t globalMeshletTriangleBase =
                    (desc.meshletTriangleCount > 0 && desc.meshletTriangleData)
                        ? m_Pool.meshlets.appendMeshletTriangles(rd, desc.meshletTriangleData, desc.meshletTriangleCount)
                        : static_cast<uint32_t>(m_Pool.meshlets.cpuMeshletTriangles.size());

                std::vector<resource::GpuMeshlet> rebasedMeshlets(desc.meshletData, desc.meshletData + desc.meshletCount);
                for (auto& meshlet : rebasedMeshlets)
                {
                    meshlet.vertexOffset += globalMeshletVertexBase;
                    meshlet.triangleOffset += globalMeshletTriangleBase;
                }

                mesh.meshletOffset = m_Pool.meshlets.appendMeshlets(rd, rebasedMeshlets.data(), desc.meshletCount);
                mesh.meshletCount  = desc.meshletCount;
            }
        }

        const uint32_t idx = static_cast<uint32_t>(m_Pool.meshes.size());
        m_Pool.meshes.push_back(std::move(mesh));
        return idx;
    }
} // namespace vultra
