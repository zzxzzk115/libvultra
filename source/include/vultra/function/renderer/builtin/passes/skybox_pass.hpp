#pragma once

#include "vultra/core/rhi/graphics_pipeline.hpp"
#include "vultra/core/rhi/render_pass.hpp"

#include <fg/Fwd.hpp>

namespace vultra
{
    namespace gfx
    {
        class SkyboxPass final : public rhi::RenderPass<SkyboxPass>
        {
            friend class BasePass;

        public:
            explicit SkyboxPass(rhi::RenderDevice&);

            FrameGraphResource addPass(FrameGraph&,
                                       FrameGraphBlackboard&,
                                       FrameGraphResource skyboxCubeMap,
                                       FrameGraphResource sceneColorHDR);

        private:
            rhi::GraphicsPipeline createPipeline(rhi::PixelFormat depthFormat, rhi::PixelFormat colorFormat) const;
        };
    } // namespace gfx
} // namespace vultra