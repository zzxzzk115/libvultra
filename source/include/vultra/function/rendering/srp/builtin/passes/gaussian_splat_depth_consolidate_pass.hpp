#pragma once

#include "vultra/core/rhi/render_pass.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"

#include <fg/Fwd.hpp>

namespace vultra
{
    class GaussianSplatDepthConsolidatePass final : public rhi::RenderPass<GaussianSplatDepthConsolidatePass>
    {
        friend class BasePass;

    public:
        GaussianSplatDepthConsolidatePass();
        FrameGraphResource addPass(FrameGraphBuildContext& ctx, FrameGraphResource depthTransmittance);

    private:
        rhi::GraphicsPipeline createPipeline(bool useSceneDepth) const;
    };
} // namespace vultra
