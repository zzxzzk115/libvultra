#pragma once

#include "vultra/core/rhi/render_pass.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"
#include "vultra/function/rendering/render_structs.hpp"

namespace vultra
{
    class HbaoPass final : public rhi::RenderPass<HbaoPass>
    {
        friend class BasePass;

    public:
        HbaoPass();
        FrameGraphResource addPass(FrameGraphBuildContext& ctx,
                                   FrameGraphResource      depth,
                                   FrameGraphResource      normal,
                                   const HbaoRenderSettings& settings);

    private:
        rhi::GraphicsPipeline createPipeline(rhi::PixelFormat colorFormat) const;
    };
} // namespace vultra
