#include "vultra/core/rhi/util.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/rhi/texture.hpp"

namespace vultra
{
    namespace rhi
    {
        namespace
        {
            // @return The first mip level of the first layer.
            [[nodiscard]] auto getDefaultRegion(const Texture& texture)
            {
                const auto extent = texture.getExtent();
                return BufferImageCopy {
                    .aspectMask        = ImageAspectFlags::eColor,
                    .layerCount        = 1,
                    .imageExtentWidth  = extent.width,
                    .imageExtentHeight = extent.height,
                    .imageExtentDepth  = 1,
                };
            }
        } // namespace

        void upload(RenderDevice&                     rd,
                    const Buffer&                     srcStagingBuffer,
                    std::span<const BufferImageCopy>  copyRegions,
                    Texture&                          dst,
                    const bool                        generateMipmaps)
        {
            rd.execute([&](CommandBuffer& cb) {
                cb.copyBuffer(srcStagingBuffer,
                              dst,
                              copyRegions.empty() ? std::array {getDefaultRegion(dst)} : copyRegions);
                if (generateMipmaps)
                    cb.generateMipmaps(dst);

                cb.getBarrierBuilder().imageBarrier(
                    {
                        .image     = dst,
                        .newLayout = ImageLayout::eReadOnly,
                        .subresourceRange =
                            ImageSubresourceRange {
                                .levelCount = UINT32_MAX,
                                .layerCount = UINT32_MAX,
                            },
                    },
                    {
                        .dstStage  = PipelineStages::eFragmentShader | PipelineStages::eComputeShader,
                        .dstAccess = Access::eShaderRead,
                    });
            });
        }

        uint32_t alignedSize(const uint32_t size, const uint32_t alignment)
        {
            return (size + alignment - 1) & ~(alignment - 1);
        }
    } // namespace rhi
} // namespace vultra
