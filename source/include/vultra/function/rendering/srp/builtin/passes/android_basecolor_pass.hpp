#pragma once

#include "vultra/core/rhi/render_pass.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"

#include <fg/Fwd.hpp>

namespace vultra
{
    class AndroidBaseColorPass final : public rhi::RenderPass<AndroidBaseColorPass>
    {
        friend class BasePass;

    public:
        FrameGraphResource addPass(FrameGraphBuildContext& ctx);

    private:
        rhi::GraphicsPipeline createPipeline(rhi::PixelFormat colorFormat, uint32_t vertexLayoutMask) const;
    };
} // namespace vultra
