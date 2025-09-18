#pragma once

#include "vultra/core/rhi/render_pass.hpp"

#include <fg/Fwd.hpp>

namespace vultra
{
    namespace gfx
    {
        class GammaCorrectionPass final : public rhi::RenderPass<GammaCorrectionPass>
        {
            friend class BasePass;

        public:
            enum class GammaCorrectionMode : uint8_t
            {
                eGamma = 0,
                eDegamma,
            };

            explicit GammaCorrectionPass(rhi::RenderDevice&);

            FrameGraphResource addPass(FrameGraph&, FrameGraphResource target, GammaCorrectionMode mode);

        private:
            rhi::GraphicsPipeline createPipeline(const rhi::PixelFormat colorFormat) const;
        };
    } // namespace gfx
} // namespace vultra