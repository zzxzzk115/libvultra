#pragma once

#include "vultra/core/rhi/render_pass.hpp"
#include "vultra/function/renderer/builtin/tonemapping_method.hpp"

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

            FrameGraphResource addPass(FrameGraph&,
                                       FrameGraphResource target,
                                       float              exposure,
                                       ToneMappingMethod  method = ToneMappingMethod::KhronosPBRNeutral);

        private:
            rhi::GraphicsPipeline createPipeline(const rhi::PixelFormat colorFormat) const;
        };
    } // namespace gfx
} // namespace vultra