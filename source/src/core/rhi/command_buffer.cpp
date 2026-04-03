#include "vultra/core/rhi/command_buffer.hpp"

namespace vultra
{
    namespace rhi
    {
        void prepareForAttachment(CommandBuffer& cb, const Texture& texture, const bool readOnly)
        {
            if (!texture)
            {
                return;
            }

            BarrierScope dst {};
            ImageLayout  newLayout {ImageLayout::eUndefined};

            const auto aspectMask = getAspectMask(texture);

            if (HasFlagValues(aspectMask, ImageAspectFlags::eColor))
            {
                dst.dstStage  = PipelineStages::eColorAttachmentOutput;
                dst.dstAccess = Access::eColorAttachmentRead | Access::eColorAttachmentWrite;
                newLayout      = ImageLayout::eAttachment;
            }
            else
            {
                dst.dstStage  = PipelineStages::eFragmentTests;
                dst.dstAccess = readOnly ? Access::eDepthStencilAttachmentRead : Access::eDepthStencilAttachmentWrite;
                newLayout      = readOnly ? ImageLayout::eReadOnly : ImageLayout::eAttachment;
            }

            cb.getBarrierBuilder().imageBarrier(
                {
                    .image     = const_cast<Texture&>(texture),
                    .newLayout = newLayout,
                    .subresourceRange =
                        ImageSubresourceRange {
                            .aspectMask     = aspectMask,
                            .baseMipLevel   = 0u,
                            .levelCount     = UINT32_MAX,
                            .baseArrayLayer = 0u,
                            .layerCount     = UINT32_MAX,
                        },
                },
                dst);
        }

        void prepareForReading(CommandBuffer& cb, const Texture& texture, uint32_t mipLevel, uint32_t layer)
        {
            if (!texture)
            {
                return;
            }
            cb.getBarrierBuilder().imageBarrier(
                {
                    .image     = const_cast<Texture&>(texture),
                    .newLayout = ImageLayout::eReadOnly,
                    .subresourceRange =
                        ImageSubresourceRange {
                            .aspectMask     = ImageAspectFlags::eNone,
                            .baseMipLevel   = mipLevel,
                            .levelCount     = mipLevel > 0 ? 1u : UINT32_MAX,
                            .baseArrayLayer = layer,
                            .layerCount     = layer > 0 ? 1u : UINT32_MAX,
                        },
                },
                {
                    .dstStage  = PipelineStages::eVertexShader | PipelineStages::eFragmentShader,
                    .dstAccess = Access::eShaderRead,
                });
        }

        void prepareForPresent(CommandBuffer& cb, const Texture& texture)
        {
            if (!texture)
            {
                return;
            }

            cb.getBarrierBuilder().imageBarrier(
                {
                    .image     = const_cast<Texture&>(texture),
                    .newLayout = ImageLayout::ePresent,
                },
                {
                    .dstStage  = PipelineStages::eColorAttachmentOutput,
                    .dstAccess = Access::eColorAttachmentWrite,
                });
        }

        void clearImageForComputing(CommandBuffer& cb, Texture& texture, const ClearValue& clearValue)
        {
            cb.getBarrierBuilder().imageBarrier(
                {
                    .image     = const_cast<Texture&>(texture),
                    .newLayout = rhi::ImageLayout::eGeneral,
                },
                {
                    .dstStage  = rhi::PipelineStages::eTransfer,
                    .dstAccess = rhi::Access::eTransferWrite,
                });
            cb.clear(texture, clearValue);
        }

        void prepareForComputing(CommandBuffer& cb, const Texture& texture)
        {
            if (!texture)
            {
                return;
            }

            cb.getBarrierBuilder().imageBarrier(
                {
                    .image     = const_cast<Texture&>(texture),
                    .newLayout = rhi::ImageLayout::eGeneral,
                },
                {
                    .dstStage  = rhi::PipelineStages::eComputeShader,
                    .dstAccess = rhi::Access::eShaderRead | rhi::Access::eShaderWrite,
                });
        }

        void prepareForRaytracing(CommandBuffer& cb, const Texture& texture)
        {
            if (!texture)
            {
                return;
            }

            cb.getBarrierBuilder().imageBarrier(
                {
                    .image     = const_cast<Texture&>(texture),
                    .newLayout = rhi::ImageLayout::eGeneral,
                },
                {
                    .dstStage  = rhi::PipelineStages::eRayTracingShader,
                    .dstAccess = rhi::Access::eShaderRead | rhi::Access::eShaderWrite,
                });
        }

        void prepareForComputing(CommandBuffer& cb, const Buffer& buffer)
        {
            assert(buffer);

            cb.getBarrierBuilder().bufferBarrier(
                {
                    .buffer = const_cast<Buffer&>(buffer),
                },
                {
                    .dstStage  = rhi::PipelineStages::eComputeShader,
                    .dstAccess = rhi::Access::eShaderRead | rhi::Access::eShaderWrite,
                });
        }

        void prepareForDrawingIndirect(CommandBuffer& cb, const Buffer& buffer)
        {
            assert(buffer);

            cb.getBarrierBuilder().bufferBarrier(
                {
                    .buffer = const_cast<Buffer&>(buffer),
                },
                {
                    .dstStage  = rhi::PipelineStages::eDrawIndirect,
                    .dstAccess = rhi::Access::eIndirectCommandRead,
                });
        }

        void prepareForReading(CommandBuffer& cb, const Buffer& buffer)
        {
            assert(buffer);

            cb.getBarrierBuilder().bufferBarrier(
                {
                    .buffer = const_cast<Buffer&>(buffer),
                    .offset = 0,
                    .size   = buffer.getSize(),
                },
                {
                    .dstStage  = rhi::PipelineStages::eVertexShader | rhi::PipelineStages::eFragmentShader,
                    .dstAccess = rhi::Access::eShaderRead,
                });
        }
    } // namespace rhi
} // namespace vultra
