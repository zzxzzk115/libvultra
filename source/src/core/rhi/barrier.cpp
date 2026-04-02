#include "vultra/core/rhi/barrier.hpp"
#include "vultra/core/rhi/texture.hpp"

namespace vultra::rhi
{
    namespace
    {
        struct SyncState
        {
            PipelineStages stage {PipelineStages::eNone};
            Access         access {Access::eNone};
        };

        [[nodiscard]] constexpr SyncState toSyncState(const BarrierScope& scope)
        {
            SyncState s {};
            s.stage  = scope.dstStage != PipelineStages::eNone ? scope.dstStage : scope.srcStage;
            s.access = scope.dstAccess != Access::eNone ? scope.dstAccess : scope.srcAccess;
            return s;
        }

        [[nodiscard]] constexpr bool isSameScope(const BarrierScope& a, const BarrierScope& b)
        {
            const auto sa = toSyncState(a);
            const auto sb = toSyncState(b);
            return sa.stage == sb.stage && sa.access == sb.access && a.dstLayout == b.dstLayout;
        }
    } // namespace

    bool Barrier::isEffective() const
    {
        return !m_MemoryBarriers.empty() || !m_BufferBarriers.empty() || !m_ImageBarriers.empty();
    }

    const std::vector<BarrierMemory>& Barrier::getMemoryBarriers() const { return m_MemoryBarriers; }

    const std::vector<BarrierBuffer>& Barrier::getBufferBarriers() const { return m_BufferBarriers; }

    const std::vector<BarrierImage>& Barrier::getImageBarriers() const { return m_ImageBarriers; }

    Barrier::Builder& Barrier::Builder::memoryBarrier(const BarrierScope& src, const BarrierScope& dst)
    {
        m_MemoryBarriers.push_back(BarrierMemory {src, dst});
        return *this;
    }

    Barrier::Builder& Barrier::Builder::bufferBarrier(const BufferInfo& info, const BarrierScope& dst)
    {
        const auto prevScope = info.buffer.getBarrierScope();
        const auto prevState = toSyncState(prevScope);
        const auto nextState = toSyncState(dst);
        if (prevState.stage != nextState.stage || prevState.access != nextState.access)
        {
            BarrierScope srcBarrier {};
            srcBarrier.srcStage  = prevState.stage;
            srcBarrier.srcAccess = prevState.access;

            BarrierScope dstBarrier {};
            dstBarrier.dstStage  = nextState.stage;
            dstBarrier.dstAccess = nextState.access;

            m_BufferBarriers.push_back(BarrierBuffer {&info.buffer, info.offset, info.size, srcBarrier, dstBarrier});

            BarrierScope storedState {};
            storedState.dstStage  = nextState.stage;
            storedState.dstAccess = nextState.access;
            info.buffer.setBarrierScope(storedState);
        }
        return *this;
    }

    Barrier::Builder& Barrier::Builder::imageBarrier(ImageInfo info, const BarrierScope& dst)
    {
        if (info.subresourceRange.aspectMask == ImageAspectFlags::eNone)
            info.subresourceRange.aspectMask = getAspectMask(info.image);

        if (info.subresourceRange.baseArrayLayer == 0u && info.subresourceRange.layerCount == UINT32_MAX &&
            info.image.getNumLayers() == 1u)
        {
            info.subresourceRange.baseArrayLayer = info.image.getBaseArrayLayer();
            info.subresourceRange.layerCount     = 1u;
        }

        const auto prevScope = info.image.getLastBarrierScope();
        const auto prevState = toSyncState(prevScope);
        const auto nextState = toSyncState(dst);
        const auto oldLayout = info.image.getImageLayout();
        if (oldLayout != info.newLayout || prevState.stage != nextState.stage || prevState.access != nextState.access)
        {
            BarrierScope srcBarrier {};
            srcBarrier.srcStage  = prevState.stage;
            srcBarrier.srcAccess = prevState.access;
            srcBarrier.srcLayout = oldLayout;

            BarrierScope dstBarrier {};
            dstBarrier.dstStage  = nextState.stage;
            dstBarrier.dstAccess = nextState.access;
            dstBarrier.dstLayout = info.newLayout;

            m_ImageBarriers.push_back(BarrierImage {
                &info.image, oldLayout, info.newLayout, info.subresourceRange, srcBarrier, dstBarrier});

            BarrierScope storedState {};
            storedState.dstStage  = nextState.stage;
            storedState.dstAccess = nextState.access;
            storedState.dstLayout = info.newLayout;
            info.image.setBarrierState(storedState, info.newLayout);
        }
        return *this;
    }

    Barrier Barrier::Builder::build() { return Barrier {std::move(*this)}; }

    Barrier::Barrier(Builder&& builder) :
        m_MemoryBarriers(std::move(builder.m_MemoryBarriers)), m_BufferBarriers(std::move(builder.m_BufferBarriers)),
        m_ImageBarriers(std::move(builder.m_ImageBarriers))
    {}
} // namespace vultra::rhi
