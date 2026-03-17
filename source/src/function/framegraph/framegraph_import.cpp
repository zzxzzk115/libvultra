#include "vultra/function/framegraph/framegraph_import.hpp"
#include "vultra/core/rhi/buffer.hpp"
#include "vultra/core/rhi/texture.hpp"
#include "vultra/function/framegraph/framegraph_buffer.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"

#include <fg/FrameGraph.hpp>

namespace vultra
{
    namespace framegraph
    {
        FrameGraphResource importBuffer(FrameGraph&            fg,
                                        const std::string_view name,
                                        rhi::Buffer*           buffer,
                                        BufferType             type,
                                        uint32_t               stride)
        {
            assert(buffer && *buffer);
            assert(stride > 0u);

            const uint64_t sizeBytes = static_cast<uint64_t>(buffer->getSize());
            const uint64_t capacity  = std::max<uint64_t>(1u, sizeBytes / stride);

            return fg.import<FrameGraphBuffer>(name,
                                               {
                                                   .type     = type,
                                                   .stride   = stride,
                                                   .capacity = capacity,
                                               },
                                               {buffer});
        }

        FrameGraphResource importTexture(FrameGraph& fg, const std::string_view name, rhi::Texture* texture)
        {
            assert(texture && *texture);
            return fg.import <FrameGraphTexture>(name,
                                                 {
                                                     .extent       = texture->getExtent(),
                                                     .depth        = texture->getDepth(),
                                                     .format       = texture->getPixelFormat(),
                                                     .numMipLevels = texture->getNumMipLevels(),
                                                     .layers       = texture->getNumLayers(),
                                                     .cubemap      = rhi::isCubemap(*texture),
                                                     .usageFlags   = texture->getUsageFlags(),
                                                 },
                                                 {texture});
        }
    } // namespace framegraph
} // namespace vultra