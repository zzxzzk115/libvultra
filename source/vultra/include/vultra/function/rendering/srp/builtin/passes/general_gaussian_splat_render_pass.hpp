#pragma once

#include "vultra/core/rhi/render_pass.hpp"
#include "vultra/core/rhi/structs/extent2d.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"

#include <fg/Fwd.hpp>

namespace vultra
{
    enum class GeneralGaussianSplatFoveatedLayer : uint32_t
    {
        eDisabled = 0,
        eFovea    = 1,
        eMid      = 2,
        eOuter    = 3,
    };

    class GeneralGaussianSplatRenderPass final : public rhi::RenderPass<GeneralGaussianSplatRenderPass>
    {
    public:
        GeneralGaussianSplatRenderPass();

        FrameGraphResource addPass(FrameGraphBuildContext& ctx);
        FrameGraphResource addFoveatedLayerPass(FrameGraphBuildContext&           ctx,
                                                GeneralGaussianSplatFoveatedLayer layer,
                                                rhi::Extent2D                    resolution,
                                                FrameGraphResource               existingColor = {});

        rhi::GraphicsPipeline createPipeline(rhi::PixelFormat colorFormat,
                                             bool             useMultiview) const;
    };
} // namespace vultra
