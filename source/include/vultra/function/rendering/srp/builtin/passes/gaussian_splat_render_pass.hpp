#pragma once

#include "vultra/core/rhi/render_pass.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"
#include "vultra/function/rendering/srp/builtin/features/gaussian_splat_feature.hpp"

#include <fg/Fwd.hpp>

namespace vultra
{
    class GaussianSplatRenderPass final : public rhi::RenderPass<GaussianSplatRenderPass>
    {
        friend class BasePass;

    public:
        GaussianSplatRenderPass();
        FrameGraphResource addPass(FrameGraphBuildContext&              ctx,
                                   FrameGraphResource                   buildToken,
                                   const GaussianSplatRendererSettings& settings,
                                   bool                                 needsSurfaceInfo);

    private:
        rhi::GraphicsPipeline createPipeline(uint64_t variantHash,
                                             bool     needsSurfaceInfo,
                                             bool     useDepthTransmittance,
                                             bool     useFragmentInterlock,
                                             bool     useSceneDepth,
                                             bool     useMultiview) const;
    };
} // namespace vultra
