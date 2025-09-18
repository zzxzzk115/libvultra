#pragma once

#include "vultra/core/rhi/graphics_pipeline.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/rhi/texture.hpp"

namespace vultra
{
    namespace gfx
    {
        class IBLDataGenerator
        {
        public:
            explicit IBLDataGenerator(rhi::RenderDevice&);
            ~IBLDataGenerator() = default;

            rhi::Texture generateBrdfLUT(rhi::CommandBuffer& cb);
            rhi::Texture generateIrradiance(rhi::CommandBuffer& cb, rhi::Texture& cubemap);
            rhi::Texture generatePrefilterEnvMap(rhi::CommandBuffer& cb, rhi::Texture& cubemap);

        private:
            rhi::GraphicsPipeline createBrdfPipeline() const;
            rhi::GraphicsPipeline createIrradiancePipeline() const;
            rhi::GraphicsPipeline createPrefilterEnvMapPipeline() const;

        private:
            rhi::RenderDevice& m_RenderDevice;

            rhi::GraphicsPipeline m_BrdfPipeline;
            rhi::GraphicsPipeline m_IrradiancePipeline;
            rhi::GraphicsPipeline m_PrefilterEnvMapPipeline;
        };
    } // namespace gfx
} // namespace vultra