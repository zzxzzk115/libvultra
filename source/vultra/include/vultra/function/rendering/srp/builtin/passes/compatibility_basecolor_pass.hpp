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
    class CompatibilityBaseColorPass final : public rhi::RenderPass<CompatibilityBaseColorPass>
    {
        friend class BasePass;

    public:
        CompatibilityBaseColorPass();
        FrameGraphResource addPass(FrameGraphBuildContext& ctx);

    private:
        rhi::GraphicsPipeline createPipeline(rhi::PixelFormat      colorFormat,
                                             bool                  webgpu,
                                             uint32_t              vertexAttributeMask,
                                             uint32_t              texCoord0Offset,
                                             uint32_t              positionOffset,
                                             uint32_t              normalOffset,
                                             uint32_t              jointIndicesOffset,
                                             uint32_t              jointWeightsOffset,
                                             uint32_t              vertexStride,
                                             uint32_t              viewMask) const;

        // Background skybox drawn first inside this pass (forward path has no separate skybox pass).
        rhi::GraphicsPipeline        createSkyboxPipeline(rhi::PixelFormat colorFormat,
                                                          bool             webgpu,
                                                          uint32_t         viewMask) const;
        const rhi::GraphicsPipeline* getSkyboxPipeline(rhi::PixelFormat colorFormat,
                                                       bool             webgpu,
                                                       uint32_t         viewMask);

        rhi::UniformBuffer& retainDrawParamBuffer(uint64_t frameIndex, rhi::UniformBuffer buffer);

        struct RetainedDrawParamBuffer
        {
            uint64_t                            frameIndex {0};
            std::unique_ptr<rhi::UniformBuffer> buffer;
        };

        std::vector<RetainedDrawParamBuffer> m_DrawParamBuffers;

        std::unique_ptr<rhi::GraphicsPipeline> m_SkyboxPipeline;
        std::size_t                            m_SkyboxPipelineKey {0};
    };
} // namespace vultra
