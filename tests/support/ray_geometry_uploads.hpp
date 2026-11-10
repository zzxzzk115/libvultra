#pragma once

#include <vultra/drivers/rhi/device.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <utility>
#include <vector>

namespace vultra::test
{
    // Observe the public VRI instance-buffer format, without exposing implementation handles for testing.
    struct RayGeometryUploads
    {
        struct Packet
        {
            std::vector<uint64_t> addresses;
            std::vector<uint32_t> flags;
            std::vector<uint32_t> ids;
        };

        explicit RayGeometryUploads(Device& device) :
            device(device),
            original(device.core)
        {
            s_Active                  = this;
            device.core.CreateBuffer  = create;
            device.core.DestroyBuffer = destroy;
            device.core.MapBuffer     = map;
            device.core.UnmapBuffer   = unmap;
        }

        ~RayGeometryUploads()
        {
            device.core = original;
            s_Active    = nullptr;
        }

        RayGeometryUploads(const RayGeometryUploads&)            = delete;
        RayGeometryUploads& operator=(const RayGeometryUploads&) = delete;

        Device&             device;
        VriCoreInterface    original;
        std::vector<Packet> packets;

    private:
        struct Mapping
        {
            const std::byte* data;
            uint64_t         size;
        };

        std::map<VriBuffer*, uint64_t>    m_Buffers;
        std::map<VriBuffer*, Mapping>     m_Mappings;
        inline static RayGeometryUploads* s_Active = nullptr;

        static VriResult VRI_CALL create(VriDevice* device, const VriBufferDesc* desc, VriBuffer** output)
        {
            auto&      self   = *s_Active;
            const auto result = self.original.CreateBuffer(device, desc, output);
            if (*output && (desc->usage & VriBufferUsage_AccelerationBuildInput) &&
                !(desc->usage & (VriBufferUsage_VertexBuffer | VriBufferUsage_IndexBuffer)))
            {
                self.m_Buffers.emplace(*output, desc->size);
            }
            return result;
        }

        static void VRI_CALL destroy(VriBuffer* buffer)
        {
            auto& self = *s_Active;
            self.m_Buffers.erase(buffer);
            self.m_Mappings.erase(buffer);
            self.original.DestroyBuffer(buffer);
        }

        static void* VRI_CALL map(VriBuffer* buffer, uint64_t offset, uint64_t size)
        {
            auto&      self  = *s_Active;
            auto*      data  = self.original.MapBuffer(buffer, offset, size);
            const auto found = self.m_Buffers.find(buffer);
            if (data && offset == 0 && found != self.m_Buffers.end() && size == found->second && size % 64 == 0)
            {
                self.m_Mappings.emplace(buffer, Mapping {static_cast<const std::byte*>(data), size});
            }
            return data;
        }

        static void VRI_CALL unmap(VriBuffer* buffer)
        {
            auto&      self  = *s_Active;
            const auto found = self.m_Mappings.find(buffer);
            if (found != self.m_Mappings.end())
            {
                Packet     packet;
                const auto count = size_t(found->second.size / 64);
                packet.addresses.resize(count);
                packet.flags.resize(count);
                packet.ids.resize(count);
                for (size_t i = 0; i < count; ++i)
                {
                    const auto* data = found->second.data + i * 64;
                    std::memcpy(&packet.ids[i], data + 48, 4);
                    std::memcpy(&packet.flags[i], data + 52, 4);
                    std::memcpy(&packet.addresses[i], data + 56, 8);
                }
                self.packets.push_back(std::move(packet));
                self.m_Mappings.erase(found);
            }
            self.original.UnmapBuffer(buffer);
        }
    };
} // namespace vultra::test
