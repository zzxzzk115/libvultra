#pragma once

#include "vultra/core/rhi/render_pass.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"

#include <fg/Fwd.hpp>

namespace vultra
{
    class GeneralGaussianSplatRenderPass final : public rhi::RenderPass<GeneralGaussianSplatRenderPass>
    {
    public:
        GeneralGaussianSplatRenderPass();

        FrameGraphResource addPass(FrameGraphBuildContext& ctx);

        rhi::GraphicsPipeline createPipeline(rhi::PixelFormat colorFormat, bool useMultiview) const;
    };
} // namespace vultra
