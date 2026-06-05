#pragma once

#include "vultra/core/rhi/render_pass.hpp"
#include "vultra/core/rhi/structs/pixel_format.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"

#include <vector>

namespace vultra
{
    // Draws GPU particle emitters as instanced, camera-facing additive billboards into the lit HDR
    // scene colour (with a soft-particle depth fade against the scene depth). Consumes the simulated
    // pool buffers produced by ParticleSimulatePass (one per GpuSceneView::particleEmitters entry).
    class ParticleRenderPass final : public rhi::RenderPass<ParticleRenderPass>
    {
        friend class BasePass;

    public:
        ParticleRenderPass();

        FrameGraphResource addPass(FrameGraphBuildContext&                ctx,
                                   FrameGraphResource                     source,
                                   FrameGraphResource                     depth,
                                   const std::vector<FrameGraphResource>& particleBuffers);

    private:
        rhi::GraphicsPipeline createPipeline(rhi::PixelFormat colorFormat) const;
    };
} // namespace vultra
