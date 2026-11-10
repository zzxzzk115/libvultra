#include <vultra/assets/scene_data.hpp>
#include <vultra/drivers/rhi/buffer_upload.hpp>
#include <vultra/servers/rendering/meshlets.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace vultra
{
    GpuMeshlets::GpuMeshlets(Device& device) :
        m_Device(device)
    {
    }

    std::expected<std::unique_ptr<GpuMeshlets>, std::string>
    GpuMeshlets::create(Device& device, Buffer& vertices, const MeshletData& data)
    {
        auto result = std::unique_ptr<GpuMeshlets>(new GpuMeshlets(device));
        if (auto initialized = result->initializeExpected(vertices, data); !initialized)
        {
            return std::unexpected(initialized.error());
        }
        return result;
    }

    GpuMeshlets::GpuMeshlets(Device& device, Buffer& vertices, const MeshletData& data) :
        GpuMeshlets(device)
    {
        if (auto initialized = initializeExpected(vertices, data); !initialized)
        {
            release();
            throw std::invalid_argument(initialized.error()); // Legacy upload boundary.
        }
    }

    std::expected<void, std::string> GpuMeshlets::initializeExpected(Buffer& vertices, const MeshletData& data)
    {
        if (!(m_Device.features & VriFeature_MeshShader))
        {
            return std::unexpected("Meshlet upload requires the enabled mesh shader feature");
        }
        if (!vertices.belongsTo(m_Device) || !vertices.handle ||
            !(vertices.desc.usage & VriBufferUsage_StorageBuffer) || data.meshlets.size() > UINT32_MAX ||
            data.vertices.size() > UINT32_MAX || data.triangles.size() > UINT32_MAX)
        {
            return std::unexpected("Meshlet upload requires a same-device storage vertex buffer and 32-bit payloads");
        }
        for (const auto& range : data.primitives)
        {
            if (uint64_t(range.first) + range.count > data.meshlets.size())
            {
                return std::unexpected("Meshlet primitive range exceeds its payload");
            }
        }
        for (const auto index : data.vertices)
        {
            if (index >= vertices.desc.size / sizeof(SceneVertex))
            {
                return std::unexpected("Meshlet vertex exceeds its scene buffer");
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
                return std::unexpected("Invalid meshlet geometry or bounds");
            }
            for (uint32_t index = 0; index < geometry.w; ++index)
            {
                const auto triangle = data.triangles[geometry.y + index];
                if ((triangle & 255) >= geometry.z || ((triangle >> 8) & 255) >= geometry.z ||
                    ((triangle >> 16) & 255) >= geometry.z)
                {
                    return std::unexpected("Meshlet triangle exceeds its local vertices");
                }
            }
        }
        count      = uint32_t(data.meshlets.size());
        primitives = data.primitives;
        const VriAccessStage ready {VriAccess_ShaderResourceRead,
                                    VriPipelineStage_TaskShader | VriPipelineStage_MeshShader};
        const std::array<std::span<const std::byte>, 3> bytes {std::as_bytes(std::span(data.meshlets)),
                                                               std::as_bytes(std::span(data.vertices)),
                                                               std::as_bytes(std::span(data.triangles))};
        const std::array<std::byte, 64>                 emptyBuffer {};
        for (size_t index = 0; index < m_Buffers.size(); ++index)
        {
            const auto payload  = bytes[index].empty() ? std::span<const std::byte>(emptyBuffer) : bytes[index];
            auto       uploaded = uploadBufferExpected(m_Device, payload, VriBufferUsage_StorageBuffer, ready);
            if (!uploaded)
            {
                return std::unexpected(uploaded.error());
            }
            m_Buffers[index] = std::move(*uploaded);
        }
        for (size_t index = 0; index < views.size(); ++index)
        {
            const auto&             buffer = index == 0 ? vertices : *m_Buffers[index - 1];
            const VriBufferViewDesc desc {buffer.handle,
                                          VriDescriptorType_StructuredBuffer,
                                          VriFormat_Unknown,
                                          0,
                                          buffer.desc.size};
            const auto              status = m_Device.core.CreateBufferView(m_Device.handle, &desc, &views[index]);
            if (status != VriResult_Success || !views[index])
            {
                return std::unexpected("Create meshlet buffer view failed: " + std::to_string(status));
            }
        }
        // Defer changing the borrowed vertex state until all candidate-owned resources and views exist.
        auto transition = Frame::create(m_Device);
        if (!transition)
        {
            return std::unexpected(transition.error());
        }
        auto commands = (*transition)->beginExpected();
        if (!commands)
        {
            return std::unexpected(commands.error());
        }
        auto       previous = vertices.captureState();
        const auto rollback = [&](std::string error) -> std::expected<void, std::string>
        {
            if (auto discarded = (*transition)->discardExpected(); !discarded)
            {
                return std::unexpected(error + "; " + discarded.error());
            }
            if (auto restored = vertices.restoreUnsubmittedExpected(std::move(previous)); !restored)
            {
                return std::unexpected(error + "; " + restored.error());
            }
            return std::unexpected(std::move(error));
        };
        if (auto changed = vertices.transitionExpected(*commands,
                                                       {VriAccess_VertexBufferRead | VriAccess_ShaderResourceRead,
                                                        VriPipelineStage_VertexInput | VriPipelineStage_MeshShader});
            !changed)
        {
            return rollback(changed.error());
        }
        if (auto submitted = (*transition)->submitExpected(); !submitted)
        {
            return rollback(submitted.error());
        }
        // An accepted submission owns this transition even if observing completion reports an error.
        return (*transition)->waitExpected();
    }

    std::array<VriBuffer*, 3> GpuMeshlets::buffers() const
    {
        return {m_Buffers[0]->handle, m_Buffers[1]->handle, m_Buffers[2]->handle};
    }

    void GpuMeshlets::release() noexcept
    {
        if (std::ranges::any_of(m_Buffers,
                                [](const auto& buffer)
                                {
                                    return bool(buffer);
                                }))
        {
            m_Device.waitIdleForDestruction();
        }
        for (auto*& view : views)
        {
            if (view)
            {
                m_Device.core.DestroyDescriptor(view);
                view = nullptr;
            }
        }
    }

    GpuMeshlets::~GpuMeshlets()
    {
        release();
    }
} // namespace vultra
