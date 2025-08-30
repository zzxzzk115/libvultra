#include "vultra/function/framegraph/transient_resources.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/base/hash.hpp"
#include "vultra/core/rhi/render_device.hpp"

#include <fmt/format.h>

namespace std
{

    template<>
    struct hash<vultra::framegraph::FrameGraphTexture::Desc>
    {
        auto operator()(const vultra::framegraph::FrameGraphTexture::Desc& desc) const noexcept
        {
            size_t h {0};
            hashCombine(h,
                        desc.extent.width,
                        desc.extent.height,
                        desc.depth,
                        desc.format,
                        desc.numMipLevels,
                        desc.layers,
                        desc.cubemap,
                        desc.usageFlags);
            return h;
        }
    };
    template<>
    struct hash<vultra::framegraph::FrameGraphBuffer::Desc>
    {
        auto operator()(const vultra::framegraph::FrameGraphBuffer::Desc& desc) const noexcept
        {
            size_t h {0};
            hashCombine(h, desc.type, desc.dataSize());
            return h;
        }
    };

} // namespace std

namespace vultra
{
    namespace framegraph
    {
        namespace
        {

            [[nodiscard]] auto getSize(const auto& pool)
            {
                VkDeviceSize total = 0;
                for (const auto& resource : pool.resources)
                {
                    total += resource->getSize();
                }
                return total;
            }

            void heartbeat(auto& pool)
            {
                // A resource's life (for how long it is going to be cached).
                constexpr auto kMaxNumFrames = 10;

                auto& [resources, entryGroups] = pool;

                auto groupsIt = entryGroups.begin();
                while (groupsIt != entryGroups.end())
                {
                    auto& [_, group] = *groupsIt;
                    if (group.empty())
                    {
                        groupsIt = entryGroups.erase(groupsIt);
                    }
                    else
                    {
                        auto entryIt = group.begin();
                        while (entryIt != group.cend())
                        {
                            auto& [resource, life] = *entryIt;
                            ++life;
                            if (life >= kMaxNumFrames)
                            {
                                VULTRA_CORE_TRACE("[FrameGraph] Deleting resource: {}", fmt::ptr(resource));
                                *resource = {};
                                entryIt   = group.erase(entryIt);
                            }
                            else
                            {
                                ++entryIt;
                            }
                        }
                        ++groupsIt;
                    }
                }

                if (!resources.empty())
                {
                    auto [ret, last] = std::ranges::remove_if(resources, [](auto& r) { return !(*r); });
                    resources.erase(ret, last);
                }
            }

        } // namespace

        TransientResources::TransientResources(rhi::RenderDevice& rd) : m_RenderDevice {rd} {}

        TransientResources::MemoryStats TransientResources::getStats() const
        {
            return MemoryStats {
                .textures = getSize(m_Textures),
                .buffers  = getSize(m_Buffers),
            };
        }

        void TransientResources::update()
        {
            heartbeat(m_Textures);
            heartbeat(m_Buffers);
        }

        rhi::Texture* TransientResources::acquireTexture(const FrameGraphTexture::Desc& desc)
        {
            const auto h = std::hash<FrameGraphTexture::Desc> {}(desc);

            if (auto& pool = m_Textures.entryGroups[h]; pool.empty())
            {
                ZoneScopedN("CreateTexture");

                auto texture =
                    rhi::Texture::Builder {}
                        .setExtent(desc.extent, desc.depth)
                        .setPixelFormat(desc.format)
                        .setNumMipLevels(desc.numMipLevels > 0 ? std::optional {desc.numMipLevels} : std::nullopt)
                        .setNumLayers(desc.layers > 0 ? std::optional {desc.layers} : std::nullopt)
                        .setUsageFlags(desc.usageFlags)
                        .setCubemap(desc.cubemap)
                        .setupOptimalSampler(false)
                        .build(m_RenderDevice);

                m_Textures.resources.emplace_back(std::make_unique<rhi::Texture>(std::move(texture)));

                auto* ptr = m_Textures.resources.back().get();
                VULTRA_CORE_TRACE("[FrameGraph] Created texture: {}", fmt::ptr(ptr));
                return ptr;
            }
            else
            {
                auto* texture = pool.back().resource;
                pool.pop_back();
                return texture;
            }
        }
        void TransientResources::releaseTexture(const FrameGraphTexture::Desc& desc, rhi::Texture* texture)
        {
            const auto h = std::hash<FrameGraphTexture::Desc> {}(desc);
            m_Textures.entryGroups[h].emplace_back(texture, 0u);
        }

        rhi::Buffer* TransientResources::acquireBuffer(const FrameGraphBuffer::Desc& desc)
        {
            assert(desc.dataSize() > 0);
            const auto h = std::hash<FrameGraphBuffer::Desc> {}(desc);

            if (auto& pool = m_Buffers.entryGroups[h]; pool.empty())
            {
                ZoneScopedN("CreateBuffer");

                Scope<rhi::Buffer> buffer;
                switch (desc.type)
                {
                    using enum BufferType;

                    case eUniformBuffer:
                        buffer =
                            std::make_unique<rhi::UniformBuffer>(m_RenderDevice.createUniformBuffer(desc.dataSize()));
                        break;
                    case eStorageBuffer:
                        buffer =
                            std::make_unique<rhi::StorageBuffer>(m_RenderDevice.createStorageBuffer(desc.dataSize()));
                        break;

                    case eVertexBuffer:
                        buffer = std::make_unique<rhi::VertexBuffer>(
                            m_RenderDevice.createVertexBuffer(desc.stride, desc.capacity));
                        break;
                    case eIndexBuffer:
                        buffer = std::make_unique<rhi::IndexBuffer>(
                            m_RenderDevice.createIndexBuffer(static_cast<rhi::IndexType>(desc.stride), desc.capacity));
                        break;

                    default:
                        assert(false);
                }
                m_Buffers.resources.push_back(std::move(buffer));
                auto* ptr = m_Buffers.resources.back().get();
                VULTRA_CORE_TRACE("[FrameGraph] Created buffer: {}", fmt::ptr(ptr));
                return ptr;
            }
            else
            {
                auto* buffer = pool.back().resource;
                pool.pop_back();
                return buffer;
            }
        }
        void TransientResources::releaseBuffer(const FrameGraphBuffer::Desc& desc, rhi::Buffer* buffer)
        {
            const auto h = std::hash<FrameGraphBuffer::Desc> {}(desc);
            m_Buffers.entryGroups[h].emplace_back(buffer, 0u);
        }
    } // namespace framegraph
} // namespace vultra