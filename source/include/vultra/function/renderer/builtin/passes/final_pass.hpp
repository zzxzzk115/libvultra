#pragma once

#include "vultra/core/rhi/render_pass.hpp"
#include "vultra/function/renderer/builtin/pass_output_mode.hpp"

#include <fg/Fwd.hpp>

namespace vultra
{
    namespace gfx
    {
        class FinalPass final : public rhi::RenderPass<FinalPass>
        {
            friend class BasePass;

        public:
            explicit FinalPass(rhi::RenderDevice&);

            FrameGraphResource
            compose(FrameGraph&, FrameGraphBlackboard&, PassOutputMode outputMode, FrameGraphResource target);

        private:
            rhi::GraphicsPipeline createPipeline(const rhi::PixelFormat colorFormat) const;
        };
    } // namespace gfx
} // namespace vultra