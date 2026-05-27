#pragma once

#include "vultra/core/rhi/structs/image_usage.hpp"
#include "vultra/core/rhi/structs/pixel_format.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/rendering/srp/render_view.hpp"

namespace vultra
{
    [[nodiscard]] inline framegraph::FrameGraphTexture::Desc
    makeRenderViewTextureDesc(const RenderView&        view,
                              const rhi::PixelFormat  format,
                              const rhi::ImageUsage   usageFlags = rhi::ImageUsage::eRenderTarget |
                                                                  rhi::ImageUsage::eSampled |
                                                                  rhi::ImageUsage::eTransferSrc)
    {
        return {
            .extent     = view.extent,
            .format     = format,
            .layers     = view.usesSingleGraphStereo() ? view.renderTargetLayerCount() : 0u,
            .viewMask   = view.usesSingleGraphStereo() ? view.renderTargetViewMask() : 0u,
            .usageFlags = usageFlags,
        };
    }

    [[nodiscard]] inline framegraph::FrameGraphTexture::Desc
    makeInheritedTextureDesc(const framegraph::FrameGraphTexture::Desc& source,
                             const rhi::PixelFormat                    format,
                             const rhi::ImageUsage                     usageFlags = rhi::ImageUsage::eRenderTarget |
                                                                                   rhi::ImageUsage::eSampled |
                                                                                   rhi::ImageUsage::eTransferSrc)
    {
        return {
            .extent     = source.extent,
            .format     = format,
            .layers     = source.layers,
            .viewMask   = source.viewMask,
            .usageFlags = usageFlags,
        };
    }

    [[nodiscard]] inline uint32_t viewMaskFromTextureDesc(const framegraph::FrameGraphTexture::Desc& desc)
    {
        return desc.viewMask;
    }
} // namespace vultra
