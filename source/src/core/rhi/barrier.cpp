#include "vultra/core/rhi/barrier.hpp"
#include "vultra/core/rhi/texture.hpp"

namespace vultra::rhi
{
    bool Barrier::isEffective() const
    {
        return !m_MemoryBarriers.empty() || !m_BufferBarriers.empty() || !m_ImageBarriers.empty();
    }

    Barrier::Builder& Barrier::Builder::memoryBarrier(const BarrierScope& src, const BarrierScope& dst)
    {
        m_MemoryBarriers.push_back(BarrierMemory {src, dst});
        return *this;
    }

    Barrier::Builder& Barrier::Builder::bufferBarrier(const BufferInfo& info, const BarrierScope& dst)
    {
        const auto src = info.buffer.getBarrierScope();
        m_BufferBarriers.push_back(BarrierBuffer {&info.buffer, info.offset, info.size, src, dst});
        info.buffer.setBarrierScope(dst);
        return *this;
    }

    Barrier::Builder& Barrier::Builder::imageBarrier(ImageInfo info, const BarrierScope& dst)
    {
        const auto src = info.image.m_LastScope;
        m_ImageBarriers.push_back(BarrierImage {&info.image, info.image.m_Layout, info.newLayout, info.subresourceRange, src, dst});
        info.image.m_LastScope = dst;
        info.image.m_Layout    = info.newLayout;
        return *this;
    }

    Barrier Barrier::Builder::build() { return Barrier {std::move(*this)}; }

    Barrier::Barrier(Builder&& builder) :
        m_MemoryBarriers(std::move(builder.m_MemoryBarriers)), m_BufferBarriers(std::move(builder.m_BufferBarriers)),
        m_ImageBarriers(std::move(builder.m_ImageBarriers))
    {}
} // namespace vultra::rhi
