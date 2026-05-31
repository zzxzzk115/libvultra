#pragma once

#include "vultra/core/rhi/render_pass.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"

namespace vultra
{
    class ThinGBufferPass final : public rhi::RenderPass<ThinGBufferPass>
    {
        friend class BasePass;

    public:
        ThinGBufferPass();
        FrameGraphResource addPass(FrameGraphBuildContext& ctx, FrameGraphResource visibility);

    private:
        rhi::GraphicsPipeline createPipeline(rhi::PixelFormat colorFormat,
                                             rhi::PixelFormat normalFormat,
                                             rhi::PixelFormat materialFormat,
                                             rhi::PixelFormat entityIdFormat,
                                             bool             writeEntityId,
                                             uint32_t         viewMask) const;
    };
} // namespace vultra
