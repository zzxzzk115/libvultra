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

        FrameGraphResource addPass(FrameGraphBuildContext& ctx, FrameGraphResource source);

    private:
        rhi::GraphicsPipeline createPipeline(rhi::PixelFormat colorFormat, uint32_t viewMask) const;
    };
} // namespace vultra
