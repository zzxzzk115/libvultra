#include "vultra/function/framegraph/framegraph_import.hpp"
#include "vultra/core/rhi/texture.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"

#include <fg/FrameGraph.hpp>

namespace vultra
{
    namespace framegraph
    {
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