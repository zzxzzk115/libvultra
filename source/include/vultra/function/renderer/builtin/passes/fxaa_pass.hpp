#pragma once

#include "vultra/core/rhi/render_pass.hpp"

#include <fg/Fwd.hpp>

namespace vultra
{
    namespace gfx
    {
        class FXAAPass final : public rhi::RenderPass<FXAAPass>
        {
            friend class BasePass;

        public:
            explicit FXAAPass(rhi::RenderDevice&);

            FrameGraphResource aa(FrameGraph&, FrameGraphResource);

        private:
            rhi::GraphicsPipeline createPipeline(const rhi::PixelFormat colorFormat) const;
        };
    } // namespace gfx
} // namespace vultra