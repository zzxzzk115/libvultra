#pragma once

#include "vultra/core/rhi/render_pass.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"

#include <fg/Fwd.hpp>

namespace vultra
{
    class GeneralGaussianSplatFoveatedCompositePass final
        : public rhi::RenderPass<GeneralGaussianSplatFoveatedCompositePass>
    {
    public:
        GeneralGaussianSplatFoveatedCompositePass();

        FrameGraphResource compose(FrameGraphBuildContext& ctx,
                                   FrameGraphResource      foveaLayer,
                                   FrameGraphResource      midLayer,
                                   FrameGraphResource      outerLayer,
                                   FrameGraphResource      baseColor = {});

        rhi::GraphicsPipeline createPipeline(rhi::PixelFormat colorFormat, bool useMultiview, bool useBase) const;
    };
} // namespace vultra
