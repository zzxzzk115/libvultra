#include "vultra/core/rhi/barrier.hpp"
#include "vultra/core/rhi/buffer.hpp"
#include "vultra/core/rhi/texture.hpp"

namespace vultra
{
    namespace rhi
    {
        namespace
        {
            void fixAspectMask(vk::ImageAspectFlags& mask, const Texture& texture)
            {
                if (mask == vk::ImageAspectFlagBits::eNone)
                    mask = getAspectMask(texture);
            }
        } // namespace

        bool Barrier::isEffective() const
        {
            return (m_Info.memoryBarrierCount + m_Info.bufferMemoryBarrierCount + m_Info.imageMemoryBarrierCount) > 0;
        }

        Barrier::Builder& Barrier::Builder::memoryBarrier(const BarrierScope& src, const BarrierScope& dst)
        {
            vk::MemoryBarrier2 memoryBarrier2 {};
            memoryBarrier2.srcStageMask  = static_cast<vk::PipelineStageFlagBits2>(src.stageMask);
            memoryBarrier2.srcAccessMask = static_cast<vk::AccessFlagBits2>(src.accessMask);
            memoryBarrier2.dstStageMask  = static_cast<vk::PipelineStageFlagBits2>(dst.stageMask);
            memoryBarrier2.dstAccessMask = static_cast<vk::AccessFlagBits2>(dst.accessMask);
            m_Dependencies.memory.emplace_back(memoryBarrier2);
            return *this;
        }

        Barrier::Builder& Barrier::Builder::bufferBarrier(const BufferInfo& info, const BarrierScope& dst)
        {
            if (auto& lastScope = info.buffer.m_LastScope; lastScope != dst)
            {
                vk::BufferMemoryBarrier2 memoryBarrier2 {};
                memoryBarrier2.srcStageMask        = static_cast<vk::PipelineStageFlagBits2>(lastScope.stageMask);
                memoryBarrier2.srcAccessMask       = static_cast<vk::AccessFlagBits2>(lastScope.accessMask);
                memoryBarrier2.dstStageMask        = static_cast<vk::PipelineStageFlagBits2>(dst.stageMask);
                memoryBarrier2.dstAccessMask       = static_cast<vk::AccessFlagBits2>(dst.accessMask);
                memoryBarrier2.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
                memoryBarrier2.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
                memoryBarrier2.buffer              = info.buffer.getHandle();
                memoryBarrier2.offset              = info.offset;
                memoryBarrier2.size                = info.size;
                m_Dependencies.buffer.emplace_back(memoryBarrier2);
                lastScope = dst;
            }
            return *this;
        }

        Barrier::Builder& Barrier::Builder::imageBarrier(ImageInfo info, const BarrierScope& dst)
        {
            auto [layout, lastScope] = std::tie(info.image.m_Layout, info.image.m_LastScope);

            if (layout != info.newLayout || lastScope != dst)
            {
                fixAspectMask(info.subresourceRange.aspectMask, info.image);
                imageBarrier(
                    info.image.getImageHandle(), lastScope, dst, layout, info.newLayout, info.subresourceRange);
                layout    = info.newLayout;
                lastScope = dst;
            }
            return *this;
        }

        Barrier Barrier::Builder::build() { return Barrier {std::move(m_Dependencies)}; }

        Barrier::Builder& Barrier::Builder::imageBarrier(vk::Image                        image,
                                                         const BarrierScope&              src,
                                                         const BarrierScope&              dst,
                                                         ImageLayout                      oldLayout,
                                                         ImageLayout                      newLayout,
                                                         const vk::ImageSubresourceRange& subresourceRange)
        {
            assert(newLayout != ImageLayout::eUndefined);

            auto& images = m_Dependencies.image;

            vk::ImageMemoryBarrier2 imageMemoryBarrier2 {};
            imageMemoryBarrier2.srcStageMask        = static_cast<vk::PipelineStageFlagBits2>(src.stageMask);
            imageMemoryBarrier2.srcAccessMask       = static_cast<vk::AccessFlagBits2>(src.accessMask);
            imageMemoryBarrier2.oldLayout           = static_cast<vk::ImageLayout>(oldLayout);
            imageMemoryBarrier2.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
            imageMemoryBarrier2.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
            imageMemoryBarrier2.image               = image;
            imageMemoryBarrier2.subresourceRange    = subresourceRange;

            auto& entry = (!images.empty() && images.back().image == image) ? images.back() :
                                                                              images.emplace_back(imageMemoryBarrier2);

            entry.dstStageMask |= static_cast<vk::PipelineStageFlagBits2>(dst.stageMask);
            entry.dstAccessMask |= static_cast<vk::AccessFlagBits2>(dst.accessMask);
            entry.newLayout = static_cast<vk::ImageLayout>(newLayout);

            return *this;
        }

        Barrier::Barrier(Dependencies&& dependencies) : m_Dependencies {std::move(dependencies)}
        {
            m_Info.memoryBarrierCount       = static_cast<uint32_t>(m_Dependencies.memory.size());
            m_Info.pMemoryBarriers          = m_Dependencies.memory.data();
            m_Info.bufferMemoryBarrierCount = static_cast<uint32_t>(m_Dependencies.buffer.size());
            m_Info.pBufferMemoryBarriers    = m_Dependencies.buffer.data();
            m_Info.imageMemoryBarrierCount  = static_cast<uint32_t>(m_Dependencies.image.size());
            m_Info.pImageMemoryBarriers     = m_Dependencies.image.data();
        }
    } // namespace rhi
} // namespace vultra