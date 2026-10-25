#include <vultra/assets/scene_data.hpp>
#include <vultra/drivers/rhi/buffer_upload.hpp>
#include <vultra/servers/rendering/meshlets.hpp>

#include <algorithm>
#include <cmath>

namespace vultra
{
    GpuMeshlets::GpuMeshlets(Device& device, Buffer& vertices, const MeshletData& data) :
        m_Device(device)
    {
        if (!(device.features & VriFeature_MeshShader))
        {
            throw std::invalid_argument("Meshlet upload requires the enabled mesh shader feature");
        }
        for (const auto& range : data.primitives)
        {
            if (uint64_t(range.first) + range.count > data.meshlets.size())
            {
                throw std::invalid_argument("Meshlet primitive range exceeds its payload");
            }
        }
        for (const auto index : data.vertices)
        {
            if (index >= vertices.desc.size / sizeof(SceneVertex))
            {
                throw std::invalid_argument("Meshlet vertex exceeds its scene buffer");
            }
        }
        for (const auto& meshlet : data.meshlets)
        {
            const auto geometry = meshlet.geometry;
            if (geometry.z > kMeshletVertices || geometry.w > kMeshletTriangles ||
                uint64_t(geometry.x) + geometry.z > data.vertices.size() ||
                uint64_t(geometry.y) + geometry.w > data.triangles.size() || !std::isfinite(meshlet.bounds.x) ||
                !std::isfinite(meshlet.bounds.y) || !std::isfinite(meshlet.bounds.z) ||
                !std::isfinite(meshlet.bounds.w) || meshlet.bounds.w < 0)
            {
                throw std::invalid_argument("Invalid meshlet geometry or bounds");
            }
            for (uint32_t index = 0; index < geometry.w; ++index)
            {
                const auto triangle = data.triangles[geometry.y + index];
                if ((triangle & 255) >= geometry.z || ((triangle >> 8) & 255) >= geometry.z ||
                    ((triangle >> 16) & 255) >= geometry.z)
                {
                    throw std::invalid_argument("Meshlet triangle exceeds its local vertices");
                }
            }
        }
        Frame transition(device);
        vertices.transition(transition.begin(),
                            {VriAccess_VertexBufferRead | VriAccess_ShaderResourceRead,
                             VriPipelineStage_VertexInput | VriPipelineStage_MeshShader});
        transition.submitAndWait();
        count      = uint32_t(data.meshlets.size());
        primitives = data.primitives;
        const VriAccessStage ready {VriAccess_ShaderResourceRead,
                                    VriPipelineStage_TaskShader | VriPipelineStage_MeshShader};
        const std::array<std::span<const std::byte>, 3> bytes {std::as_bytes(std::span(data.meshlets)),
                                                               std::as_bytes(std::span(data.vertices)),
                                                               std::as_bytes(std::span(data.triangles))};
        try
        {
            const std::array<std::byte, 64> emptyBuffer {};
            for (size_t i = 0; i < m_Buffers.size(); ++i)
            {
                const auto data = bytes[i].empty() ? std::span<const std::byte>(emptyBuffer) : bytes[i];
                m_Buffers[i]    = uploadBuffer(device, data, VriBufferUsage_StorageBuffer, ready);
            }
            for (size_t i = 0; i < views.size(); ++i)
            {
                const auto&             buffer = i == 0 ? vertices : *m_Buffers[i - 1];
                const VriBufferViewDesc desc {buffer.handle,
                                              VriDescriptorType_StructuredBuffer,
                                              VriFormat_Unknown,
                                              0,
                                              buffer.desc.size};
                check(device.core.CreateBufferView(device.handle, &desc, &views[i]), "Create meshlet buffer view");
            }
        }
        catch (...)
        {
            for (auto* view : views)
            {
                if (view)
                {
                    device.core.DestroyDescriptor(view);
                }
            }
            throw;
        }
    }

    std::array<VriBuffer*, 3> GpuMeshlets::buffers() const
    {
        return {m_Buffers[0]->handle, m_Buffers[1]->handle, m_Buffers[2]->handle};
    }

    GpuMeshlets::~GpuMeshlets()
    {
        m_Device.waitIdleForDestruction();
        for (auto* view : views)
        {
            m_Device.core.DestroyDescriptor(view);
        }
    }
} // namespace vultra
