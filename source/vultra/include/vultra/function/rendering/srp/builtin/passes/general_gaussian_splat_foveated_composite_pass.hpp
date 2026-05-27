#pragma once

#include "vultra/core/rhi/render_pass.hpp"
#include "vultra/core/rhi/uniform_buffer.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"

#include <fg/Fwd.hpp>

#include <cstdint>

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

        rhi::GraphicsPipeline createPipeline(rhi::PixelFormat colorFormat, uint32_t viewMask, bool useBase) const;

    private:
        rhi::UniformBuffer m_UniformBuffer;
    };
} // namespace vultra
