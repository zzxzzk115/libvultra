#pragma once

#include "vultra/core/rhi/render_pass.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"

namespace vultra
{
    class SsrCompositePass final : public rhi::RenderPass<SsrCompositePass>
    {
        friend class BasePass;

    public:
        SsrCompositePass();
        FrameGraphResource addPass(FrameGraphBuildContext& ctx, FrameGraphResource source, FrameGraphResource reflection);

    private:
        rhi::GraphicsPipeline createPipeline(rhi::PixelFormat colorFormat, uint32_t viewMask) const;
    };
} // namespace vultra
