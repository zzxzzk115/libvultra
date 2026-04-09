#pragma once

#include "vultra/core/rhi/render_pass.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"

#include <fg/Fwd.hpp>

namespace vultra
{
    class CompatibilityBaseColorPass final : public rhi::RenderPass<CompatibilityBaseColorPass>
    {
        friend class BasePass;

    public:
        CompatibilityBaseColorPass();
        FrameGraphResource addPass(FrameGraphBuildContext& ctx);

    private:
        rhi::GraphicsPipeline createPipeline(rhi::PixelFormat      colorFormat,
                                             bool                  webgpu,
                                             uint32_t              texCoord0Offset,
                                             uint32_t              positionOffset,
                                             uint32_t              vertexStride) const;
    };
} // namespace vultra
