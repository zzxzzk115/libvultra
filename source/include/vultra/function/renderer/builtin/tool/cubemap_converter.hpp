#pragma once

#include "vultra/core/rhi/compute_pipeline.hpp"
#include "vultra/core/rhi/render_device.hpp"

namespace vultra
{
    namespace gfx
    {
        class CubemapConverter
        {
        public:
            explicit CubemapConverter(rhi::RenderDevice&);
            ~CubemapConverter() = default;

            [[nodiscard]] Ref<rhi::Texture> convertToCubemap(rhi::CommandBuffer& cb, rhi::Texture& equirectangularMap);

        private:
            rhi::RenderDevice&   m_RenderDevice;
            rhi::ComputePipeline m_ConvertPipeline;
        };
    } // namespace gfx
} // namespace vultra