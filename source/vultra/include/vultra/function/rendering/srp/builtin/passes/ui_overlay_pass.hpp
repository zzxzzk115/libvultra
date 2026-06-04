#pragma once

#include "vultra/core/rhi/render_pass.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"

namespace vultra
{
    class UiOverlayPass final : public rhi::RenderPass<UiOverlayPass>
    {
        friend class BasePass;

    public:
        UiOverlayPass();

        // `depth` is optional: when supplied (a producing pass wired to the "depth" input)
        // world-space UI is depth-tested against scene geometry; screen-overlay UI (z=0)
        // always passes and stays on top.
        FrameGraphResource addPass(FrameGraphBuildContext& ctx, FrameGraphResource source, FrameGraphResource depth = {});

    private:
        rhi::GraphicsPipeline
        createPipeline(rhi::PixelFormat colorFormat, uint32_t viewMask, rhi::PixelFormat depthFormat) const;
    };
} // namespace vultra
