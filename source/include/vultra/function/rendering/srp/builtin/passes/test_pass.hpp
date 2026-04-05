#pragma once

#include "vultra/core/rhi/render_pass.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"

#include <fg/Fwd.hpp>

namespace vultra
{
    class TestPass final : public rhi::RenderPass<TestPass>
    {
        friend class BasePass;

    public:
        TestPass();
        FrameGraphResource addPass(FrameGraphBuildContext& ctx);

    private:
        rhi::GraphicsPipeline createPipeline(const rhi::PixelFormat colorFormat) const;
    };
} // namespace vultra
