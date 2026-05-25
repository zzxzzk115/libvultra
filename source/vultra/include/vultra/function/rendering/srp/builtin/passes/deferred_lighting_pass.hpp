#pragma once

#include "vultra/core/rhi/compute_pipeline.hpp"
#include "vultra/core/rhi/render_pass.hpp"
#include "vultra/core/rhi/texture.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"
#include "vultra/function/rendering/render_structs.hpp"

#include <fg/Fwd.hpp>

#include <string_view>

namespace vultra
{
    class DeferredLightingPass final : public rhi::RenderPass<DeferredLightingPass>
    {
        friend class BasePass;

    public:
        DeferredLightingPass();
        FrameGraphResource addPass(FrameGraphBuildContext& ctx,
                                   FrameGraphResource      color,
                                   FrameGraphResource      normal,
                                   FrameGraphResource      material,
                                   FrameGraphResource      depth,
                                   FrameGraphResource      ssao,
                                   FrameGraphResource      shadowMap,
                                   FrameGraphResource      shadowData,
                                   const ShadowRenderSettings& shadowSettings,
                                   const PbrLightingSettings& lightingSettings,
                                   const RenderWorld* renderWorld);

        [[nodiscard]] rhi::Texture* environmentCubemap() { return &m_EnvironmentCubemap; }

    private:
        bool ensureBuiltinLtcTextures(rhi::RenderDevice& rd);
        bool ensureIblTextures(rhi::CommandBuffer& cb, rhi::RenderDevice& rd, const PbrLightingSettings& lightingSettings);
        bool ensureFallbackIblTextures(rhi::CommandBuffer& cb,
                                       rhi::RenderDevice&  rd,
                                       const PbrLightingSettings& lightingSettings);
        bool ensureEnvironmentIblTextures(rhi::CommandBuffer& cb,
                                          rhi::RenderDevice&  rd,
                                          const PbrLightingSettings& lightingSettings);
        bool ensureFallbackAoTexture(rhi::RenderDevice& rd);
        rhi::GraphicsPipeline createPipeline(rhi::PixelFormat colorFormat) const;
        rhi::ComputePipeline  createComputePipeline(std::string_view shaderName) const;

        rhi::Texture m_LtcMat;
        rhi::Texture m_LtcMag;
        rhi::Texture m_FallbackBrdfLut;
        rhi::Texture m_FallbackIrradianceMap;
        rhi::Texture m_FallbackPrefilteredEnvMap;
        rhi::Texture m_FallbackAo;
        glm::vec3    m_FallbackIblColor {0.0f};

        rhi::Texture* m_EnvironmentSource {nullptr};
        rhi::Texture  m_EnvironmentCubemap;
        rhi::Texture  m_EnvironmentBrdfLut;
        rhi::Texture  m_EnvironmentIrradianceMap;
        rhi::Texture  m_EnvironmentPrefilteredEnvMap;

        rhi::ComputePipeline m_CubemapConvertPipeline;
        rhi::ComputePipeline m_BrdfPipeline;
        rhi::ComputePipeline m_IrradiancePipeline;
        rhi::ComputePipeline m_PrefilterPipeline;
    };
} // namespace vultra
