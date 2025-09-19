#pragma once

#include "vultra/core/rhi/compute_pipeline.hpp"
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
            [[nodiscard]] Ref<rhi::Texture> generateIrradianceMap(rhi::CommandBuffer& cb, rhi::Texture& cubemap);
            [[nodiscard]] Ref<rhi::Texture> generatePrefilterEnvMap(rhi::CommandBuffer& cb, rhi::Texture& cubemap);

            [[nodiscard]] bool isBrdfLUTPresent() const { return m_BrdfLUTGenerated; }

        private:
            rhi::ComputePipeline createBrdfPipeline() const;
            rhi::ComputePipeline createIrradiancePipeline() const;
            rhi::ComputePipeline createPrefilterEnvMapPipeline() const;

        private:
            rhi::RenderDevice& m_RenderDevice;

            rhi::ComputePipeline m_BrdfPipeline;
            rhi::ComputePipeline m_IrradiancePipeline;
            rhi::ComputePipeline m_PrefilterEnvMapPipeline;

            bool m_BrdfLUTGenerated {false};
        };
    } // namespace gfx
} // namespace vultra