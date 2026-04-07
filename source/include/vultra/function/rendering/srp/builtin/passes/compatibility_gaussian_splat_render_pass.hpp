#pragma once

#include "vultra/core/rhi/render_pass.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"
#include "vultra/function/rendering/srp/builtin/features/gaussian_splat_feature.hpp"

#include <fg/Fwd.hpp>

namespace vultra
{
    class CompatibilityGaussianSplatRenderPass final : public rhi::RenderPass<CompatibilityGaussianSplatRenderPass>
    {
        friend class BasePass;

    public:
        CompatibilityGaussianSplatRenderPass();
        FrameGraphResource addPass(FrameGraphBuildContext&              ctx,
                                   FrameGraphResource                   buildToken,
                                   FrameGraphResource                   target,
                                   const GaussianSplatRendererSettings& settings);

    private:
        rhi::GraphicsPipeline createPipeline(rhi::PixelFormat colorFormat, bool useSortedIds) const;
    };
} // namespace vultra
