#pragma once

#include "vultra/core/rhi/render_pass.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"

#include <fg/Fwd.hpp>

namespace vultra
{
    class FinalCompositionPass final : public rhi::RenderPass<FinalCompositionPass>
    {
        friend class BasePass;

    public:
        FinalCompositionPass();
        FrameGraphResource compose(FrameGraphBuildContext& ctx, FrameGraphResource target);

    private:
        rhi::GraphicsPipeline createPipeline(rhi::PixelFormat colorFormat, bool useMultiview, bool debugEntityIdOutput) const;
    };
} // namespace vultra
