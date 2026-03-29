#pragma once

#include "vultra/core/rhi/render_pass.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"

#include <fg/Fwd.hpp>

namespace vultra
{
    // Blends the gaussian-splat color layer on top of the opaque meshlet layer.
    // Uses premultiplied-alpha over: result.rgb = splat.rgb + meshlet.rgb * (1 - splat.a)
    class SplatCompositePass final : public rhi::RenderPass<SplatCompositePass>
    {
        friend class BasePass;

    public:
        FrameGraphResource addPass(FrameGraphBuildContext& ctx,
                                   FrameGraphResource      meshletColor,
                                   FrameGraphResource      splatColor,
                                   FrameGraphResource      splatDepthAccum,
                                   FrameGraphResource      sceneDepth);

    private:
        rhi::GraphicsPipeline createPipeline(rhi::PixelFormat colorFormat, bool useDepthAware) const;
    };
} // namespace vultra
