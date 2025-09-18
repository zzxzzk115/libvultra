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

            [[nodiscard]] Ref<rhi::Texture> generateBrdfLUT(rhi::CommandBuffer& cb);
            [[nodiscard]] Ref<rhi::Texture> generateIrradiance(rhi::CommandBuffer& cb, rhi::Texture& cubemap);
            [[nodiscard]] Ref<rhi::Texture> generatePrefilterEnvMap(rhi::CommandBuffer& cb, rhi::Texture& cubemap);

            [[nodiscard]] bool isBrdfLUTPresent() const { return m_BrdfLUTGenerated; }

        private:
            rhi::GraphicsPipeline createBrdfPipeline() const;
            rhi::GraphicsPipeline createIrradiancePipeline() const;
            rhi::GraphicsPipeline createPrefilterEnvMapPipeline() const;

        private:
            rhi::RenderDevice& m_RenderDevice;

            rhi::GraphicsPipeline m_BrdfPipeline;
            rhi::GraphicsPipeline m_IrradiancePipeline;
            rhi::GraphicsPipeline m_PrefilterEnvMapPipeline;

            bool m_BrdfLUTGenerated {false};
        };
    } // namespace gfx
} // namespace vultra