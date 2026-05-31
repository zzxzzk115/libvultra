#pragma once

#include "vultra/core/rhi/render_pass.hpp"
#include "vultra/core/rhi/uniform_buffer.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"

#include <fg/Fwd.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace vultra
{
    class DirectGBufferPass final : public rhi::RenderPass<DirectGBufferPass>
    {
        friend class BasePass;

    public:
        DirectGBufferPass();
        FrameGraphResource addDepthPrePass(FrameGraphBuildContext& ctx);
        FrameGraphResource addPass(FrameGraphBuildContext& ctx, FrameGraphResource prepassDepth = {});

    private:
        rhi::GraphicsPipeline createPipeline(bool             depthOnly,
                                             rhi::PixelFormat colorFormat,
                                             rhi::PixelFormat normalFormat,
                                             rhi::PixelFormat materialFormat,
                                             rhi::PixelFormat entityIdFormat,
                                             bool             writeEntityId,
                                             bool             readOnlyDepth,
                                             uint32_t         vertexAttributeMask,
                                             uint32_t         positionOffset,
                                             uint32_t         normalOffset,
                                             uint32_t         texCoord0Offset,
                                             uint32_t         tangentOffset,
                                             bool             doubleSided,
                                             uint32_t         vertexStride,
                                             uint32_t         viewMask) const;

        rhi::UniformBuffer& retainDrawParamBuffer(uint64_t frameIndex, rhi::UniformBuffer buffer);

        struct RetainedDrawParamBuffer
        {
            uint64_t                            frameIndex {0};
            std::unique_ptr<rhi::UniformBuffer> buffer;
        };

        std::vector<RetainedDrawParamBuffer> m_DrawParamBuffers;
    };
} // namespace vultra
