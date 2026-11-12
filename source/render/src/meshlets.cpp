#include <vultra/assets/scene_data.hpp>
#include <vultra/gpu/buffer_upload.hpp>
#include <vultra/render/meshlets.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace vultra
{
    GpuMeshlets::GpuMeshlets(Device& device) :
        m_Device(device)
    {
    }

    std::expected<std::unique_ptr<GpuMeshlets>, Error>
    GpuMeshlets::create(Device& device, Buffer& vertices, const MeshletData& data)
    {
        if (!device.features().meshShading)
        {
            return makeError("Meshlet upload requires the enabled mesh shader feature");
        }
        for (const auto& range : data.primitives)
        {
            if (uint64_t(range.first) + range.count > data.meshlets.size())
            {
                return makeError("Meshlet primitive range exceeds its payload");
            }
        }
        for (const auto index : data.vertices)
        {
            if (index >= vertices.desc.size / sizeof(SceneVertex))
            {
                return makeError("Meshlet vertex exceeds its scene buffer");
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
                return makeError("Invalid meshlet geometry or bounds");
            }
            for (uint32_t index = 0; index < geometry.w; ++index)
            {
                const auto triangle = data.triangles[geometry.y + index];
                if ((triangle & 255) >= geometry.z || ((triangle >> 8) & 255) >= geometry.z ||
                    ((triangle >> 16) & 255) >= geometry.z)
                {
                    return makeError("Meshlet triangle exceeds its local vertices");
                }
            }
        }
        auto transition = Frame::create(device);
        if (!transition)
        {
            return std::unexpected(transition.error());
        }
        const auto cmd = (*transition)->begin();
        if (!cmd)
        {
            return std::unexpected(cmd.error());
        }
        vertices.transition(*cmd,
                            {VriAccess_VertexBufferRead | VriAccess_ShaderResourceRead,
                             VriPipelineStage_VertexInput | VriPipelineStage_MeshShader});
        if (auto submitted = (*transition)->submitAndWait(); !submitted)
        {
            return std::unexpected(submitted.error());
        }
        // The destructor releases whatever views exist when a later step fails.
        std::unique_ptr<GpuMeshlets> result(new GpuMeshlets(device));
        result->count      = uint32_t(data.meshlets.size());
        result->primitives = data.primitives;
        const VriAccessStage                            ready {VriAccess_ShaderResourceRead,
                                    VriPipelineStage_TaskShader | VriPipelineStage_MeshShader};
        const std::array<std::span<const std::byte>, 3> bytes {std::as_bytes(std::span(data.meshlets)),
                                                               std::as_bytes(std::span(data.vertices)),
                                                               std::as_bytes(std::span(data.triangles))};
        const std::array<std::byte, 64>                 emptyBuffer {};
        for (size_t i = 0; i < result->m_Buffers.size(); ++i)
        {
            const auto payload  = bytes[i].empty() ? std::span<const std::byte>(emptyBuffer) : bytes[i];
            auto       uploaded = uploadBuffer(device, payload, VriBufferUsage_StorageBuffer, ready);
            if (!uploaded)
            {
                return std::unexpected(uploaded.error());
            }
            result->m_Buffers[i] = std::make_unique<Buffer>(std::move(*uploaded));
        }
        for (size_t i = 0; i < result->views.size(); ++i)
        {
            const auto&             buffer = i == 0 ? vertices : *result->m_Buffers[i - 1];
            const VriBufferViewDesc desc {buffer.handle,
                                          VriDescriptorType_StructuredBuffer,
                                          VriFormat_Unknown,
                                          0,
                                          buffer.desc.size};
            if (auto created = check(device.core.CreateBufferView(device.handle, &desc, &result->views[i]),
                                     "Create meshlet buffer view");
                !created)
            {
                return std::unexpected(created.error());
            }
        }
        return result;
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
            if (view)
            {
                m_Device.core.DestroyDescriptor(view);
            }
        }
    }
} // namespace vultra
