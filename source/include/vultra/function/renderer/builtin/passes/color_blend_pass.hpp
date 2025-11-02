#pragma once

#include <vultra/core/rhi/render_pass.hpp>

#include <fg/Fwd.hpp>

namespace vultra
{
    namespace gfx
    {
        enum class BlendType
        {
            eNormal = 0,
            eAdditive,
        };

        enum class ColorRange
        {
            eLDR,
            eHDR,
        };

        class ColorBlendPass final : public rhi::RenderPass<ColorBlendPass>
        {
            friend class BasePass;

        public:
            explicit ColorBlendPass(rhi::RenderDevice&);

            FrameGraphResource
            blend(FrameGraph&, FrameGraphResource src, FrameGraphResource dst, BlendType, ColorRange);

        private:
            rhi::GraphicsPipeline createPipeline(const rhi::PixelFormat colorFormat, BlendType) const;
        };
    } // namespace gfx
} // namespace vultra