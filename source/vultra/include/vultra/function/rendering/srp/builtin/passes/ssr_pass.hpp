#pragma once

#include "vultra/core/rhi/render_pass.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"
#include "vultra/function/rendering/render_structs.hpp"

namespace vultra
{
    class SsrPass final : public rhi::RenderPass<SsrPass>
    {
        friend class BasePass;

    public:
        SsrPass();
        FrameGraphResource addPass(FrameGraphBuildContext&    ctx,
                                   FrameGraphResource         color,
                                   FrameGraphResource         depth,
                                   FrameGraphResource         normal,
                                   FrameGraphResource         material,
                                   const SsrRenderSettings&   settings);

    private:
        rhi::GraphicsPipeline createPipeline(rhi::PixelFormat colorFormat) const;
    };
} // namespace vultra
