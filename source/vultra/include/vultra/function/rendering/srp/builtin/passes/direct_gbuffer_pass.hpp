#pragma once

#include "vultra/core/rhi/render_pass.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"

#include <fg/Fwd.hpp>

namespace vultra
{
    class DirectGBufferPass final : public rhi::RenderPass<DirectGBufferPass>
    {
        friend class BasePass;

    public:
        DirectGBufferPass();
        FrameGraphResource addPass(FrameGraphBuildContext& ctx);

    private:
        rhi::GraphicsPipeline createPipeline(rhi::PixelFormat colorFormat,
                                             rhi::PixelFormat normalFormat,
                                             rhi::PixelFormat materialFormat,
                                             uint32_t         positionOffset,
                                             uint32_t         normalOffset,
                                             uint32_t         texCoord0Offset,
                                             uint32_t         tangentOffset,
                                             bool             hasTangent,
                                             bool             doubleSided,
                                             uint32_t         vertexStride) const;
    };
} // namespace vultra
