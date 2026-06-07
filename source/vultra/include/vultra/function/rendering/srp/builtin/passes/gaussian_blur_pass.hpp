#pragma once

#include "vultra/core/rhi/render_pass.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"

namespace vultra
{
    // Separable Gaussian blur (linear-sampled). One addPass performs a single direction;
    // the convenience overload runs horizontal then vertical for a full blur.
    class GaussianBlurPass final : public rhi::RenderPass<GaussianBlurPass>
    {
        friend class BasePass;

    public:
        GaussianBlurPass();

        FrameGraphResource
        addPass(FrameGraphBuildContext& ctx, FrameGraphResource source, float scale, bool horizontal);

        // Full separable blur (horizontal pass followed by vertical pass).
        FrameGraphResource addPass(FrameGraphBuildContext& ctx, FrameGraphResource source, float scale = 1.0f);

    private:
        rhi::GraphicsPipeline createPipeline(rhi::PixelFormat colorFormat, uint32_t viewMask) const;
    };
} // namespace vultra
