#pragma once

#include "vultra/core/rhi/render_pass.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"
#include "vultra/function/rendering/srp/builtin/passes/gaussian_blur_pass.hpp"

namespace vultra
{
    // HDR bloom: bright/threshold extraction -> separable Gaussian blur -> additive combine.
    // Pairs naturally with the HDR emissive GBuffer (threshold ~1.0 makes >1 values bloom).
    class BloomPass final : public rhi::RenderPass<BloomPass>
    {
        friend class BasePass;

    public:
        BloomPass();

        FrameGraphResource addPass(FrameGraphBuildContext& ctx,
                                   FrameGraphResource      source,
                                   float                   threshold,
                                   float                   knee,
                                   float                   intensity,
                                   float                   blurScale,
                                   int                     iterations);

    private:
        // stage: 0 = prefilter (bright extract), 1 = combine.
        rhi::GraphicsPipeline createPipeline(rhi::PixelFormat colorFormat, uint32_t viewMask, uint32_t stage) const;

        GaussianBlurPass m_Blur;
    };
} // namespace vultra
