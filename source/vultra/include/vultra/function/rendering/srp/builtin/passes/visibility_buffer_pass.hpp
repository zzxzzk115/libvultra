#pragma once

#include "vultra/core/rhi/render_pass.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"

namespace vultra
{
    class VisibilityBufferPass final : public rhi::RenderPass<VisibilityBufferPass>
    {
        friend class BasePass;

    public:
        VisibilityBufferPass();
        FrameGraphResource addPass(FrameGraphBuildContext& ctx);

    private:
        rhi::GraphicsPipeline createPipeline() const;
    };
} // namespace vultra
