#pragma once

#include "vultra/core/rhi/render_pass.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"

namespace vultra
{
    class MotionVectorPass final : public rhi::RenderPass<MotionVectorPass>
    {
        friend class BasePass;

    public:
        MotionVectorPass();
        FrameGraphResource addPass(FrameGraphBuildContext& ctx, FrameGraphResource depth);

    private:
        rhi::GraphicsPipeline createPipeline(rhi::PixelFormat colorFormat, uint32_t viewMask) const;
    };
} // namespace vultra
