#pragma once

#include "vultra/core/rhi/render_pass.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"
#include "vultra/function/rendering/render_structs.hpp"

namespace vultra
{
    class SsaoPass final : public rhi::RenderPass<SsaoPass>
    {
        friend class BasePass;

    public:
        SsaoPass();
        FrameGraphResource addPass(FrameGraphBuildContext& ctx,
                                   FrameGraphResource      depth,
                                   FrameGraphResource      normal,
                                   const SsaoRenderSettings& settings);

    private:
        rhi::GraphicsPipeline createPipeline(rhi::PixelFormat colorFormat) const;
    };
} // namespace vultra
