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
                return VkBufferImageCopy {
                    .imageSubresource =
                        {
                            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                            .layerCount = 1,
                        },
                    .imageExtent =
                        {
                            .width  = extent.width,
                            .height = extent.height,
                            .depth  = 1,
                        },
                };
            }
        } // namespace

        void upload(RenderDevice&                        rd,
                    const Buffer&                        srcStagingBuffer,
                    std::span<const vk::BufferImageCopy> copyRegions,
                    Texture&                             dst,
                    const bool                           generateMipmaps)
        {
            rd.execute([&](CommandBuffer& cb) {
                cb.copyBuffer(srcStagingBuffer,
                              dst,
                              copyRegions.empty() ? std::array {vk::BufferImageCopy(getDefaultRegion(dst))} :
                                                    copyRegions);
                if (generateMipmaps)
                    cb.generateMipmaps(dst);

                cb.getBarrierBuilder().imageBarrier(
                    {
                        .image     = dst,
                        .newLayout = ImageLayout::eReadOnly,
                        .subresourceRange =
                            VkImageSubresourceRange {
                                .levelCount = VK_REMAINING_MIP_LEVELS,
                                .layerCount = VK_REMAINING_ARRAY_LAYERS,
                            },
                    },
                    {
                        .stageMask  = PipelineStages::eFragmentShader | PipelineStages::eComputeShader,
                        .accessMask = Access::eShaderRead,
                    });
            });
        }
    } // namespace rhi
} // namespace vultra