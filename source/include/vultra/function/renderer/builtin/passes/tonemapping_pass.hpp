#pragma once

#include "vultra/core/rhi/render_pass.hpp"

#include <fg/Fwd.hpp>

namespace vultra
{
    namespace gfx
    {
        class ToneMappingPass final : public rhi::RenderPass<ToneMappingPass>
        {
            friend class BasePass;

        public:
            explicit ToneMappingPass(rhi::RenderDevice&);

            FrameGraphResource addPass(FrameGraph&, FrameGraphResource target);

        private:
            rhi::GraphicsPipeline createPipeline(const rhi::PixelFormat colorFormat) const;
        };
    } // namespace gfx
} // namespace vultra