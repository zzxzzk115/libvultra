#pragma once

#include "vultra/core/rhi/render_pass.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"

#include <fg/Fwd.hpp>

namespace vultra
{
    class WebGPUBaseColorPass final : public rhi::RenderPass<WebGPUBaseColorPass>
    {
        friend class BasePass;

    public:
        void addPass(FrameGraphBuildContext& ctx, FrameGraphResource target);

    private:
        rhi::GraphicsPipeline
        createPipeline(rhi::PixelFormat colorFormat,
                       uint32_t         texCoord0Offset,
                       uint32_t         positionOffset,
                       uint32_t         vertexStride,
                       bool             textured) const;
    };
} // namespace vultra
