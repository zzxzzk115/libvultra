#pragma once

#include "vultra/core/rhi/render_pass.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"

namespace vultra
{
    class ToneMappingPass final : public rhi::RenderPass<ToneMappingPass>
    {
        friend class BasePass;

    public:
        ToneMappingPass();
        FrameGraphResource addPass(FrameGraphBuildContext& ctx,
                                   FrameGraphResource      source,
                                   float                   exposure = 1.0f,
                                   int                     method   = 0);

    private:
        rhi::GraphicsPipeline createPipeline(rhi::PixelFormat colorFormat, uint32_t viewMask) const;
    };
} // namespace vultra
