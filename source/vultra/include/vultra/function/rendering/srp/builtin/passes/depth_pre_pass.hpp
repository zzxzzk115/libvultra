#pragma once

#include "vultra/core/rhi/render_pass.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"

namespace vultra
{
    class DepthPrePass final : public rhi::RenderPass<DepthPrePass>
    {
        friend class BasePass;

    public:
        DepthPrePass();
        void addPass(FrameGraphBuildContext& ctx);

    private:
        rhi::GraphicsPipeline createPipeline(uint32_t viewMask) const;
    };
} // namespace vultra
