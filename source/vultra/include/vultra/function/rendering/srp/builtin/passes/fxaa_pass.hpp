#pragma once

#include "vultra/core/rhi/render_pass.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"

namespace vultra
{
    class FxaaPass final : public rhi::RenderPass<FxaaPass>
    {
        friend class BasePass;

    public:
        FxaaPass();
        FrameGraphResource addPass(FrameGraphBuildContext& ctx, FrameGraphResource source);

    private:
        rhi::GraphicsPipeline createPipeline(rhi::PixelFormat colorFormat) const;
    };
} // namespace vultra
