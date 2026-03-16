#pragma once

#include "vultra/core/rhi/render_pass.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"

#include <fg/Fwd.hpp>

namespace vultra
{
    class GaussianSplatRenderPass final : public rhi::RenderPass<GaussianSplatRenderPass>
    {
        friend class BasePass;

    public:
        FrameGraphResource addPass(FrameGraphBuildContext& ctx, FrameGraphResource buildToken);

    private:
        rhi::GraphicsPipeline createPipeline(uint64_t variantHash) const;
    };
} // namespace vultra
