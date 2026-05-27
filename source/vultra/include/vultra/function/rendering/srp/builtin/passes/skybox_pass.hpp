#pragma once

#include "vultra/core/rhi/render_pass.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"

#include <fg/Fwd.hpp>

namespace vultra
{
    namespace rhi
    {
        class Texture;
    }

    class SkyboxPass final : public rhi::RenderPass<SkyboxPass>
    {
        friend class BasePass;

    public:
        SkyboxPass();

        FrameGraphResource addPass(FrameGraphBuildContext& ctx,
                                   FrameGraphResource      color,
                                   FrameGraphResource      depth,
                                   FrameGraphResource      environmentMap,
                                   rhi::Texture*           cubemapOverride = nullptr);

    private:
        rhi::GraphicsPipeline createPipeline(rhi::PixelFormat depthFormat,
                                             rhi::PixelFormat colorFormat,
                                             uint32_t         viewMask) const;
    };
} // namespace vultra
