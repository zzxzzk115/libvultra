#pragma once

#include "vultra/core/rhi/render_pass.hpp"

#include <fg/Fwd.hpp>

namespace vultra
{
    namespace gfx
    {
        class BlitPass final : public rhi::RenderPass<BlitPass>
        {
            friend class BasePass;

        public:
            explicit BlitPass(rhi::RenderDevice&);

            void blit(FrameGraph&, FrameGraphResource src, FrameGraphResource dst, bool readyForReading = false);

        private:
            rhi::GraphicsPipeline createPipeline(const rhi::PixelFormat colorFormat) const;
        };
    } // namespace gfx
} // namespace vultra
