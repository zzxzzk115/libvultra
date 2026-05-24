#pragma once

#include "vultra/core/rhi/render_pass.hpp"
#include "vultra/core/rhi/texture.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"
#include "vultra/function/rendering/render_structs.hpp"

#include <fg/Fwd.hpp>

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

    private:
        bool ensureBuiltinLtcTextures(rhi::RenderDevice& rd);
        bool ensureFallbackIblTextures(rhi::RenderDevice& rd, const PbrLightingSettings& lightingSettings);
        bool ensureFallbackAoTexture(rhi::RenderDevice& rd);
        rhi::GraphicsPipeline createPipeline(rhi::PixelFormat colorFormat) const;

        rhi::Texture m_LtcMat;
        rhi::Texture m_LtcMag;
        rhi::Texture m_FallbackBrdfLut;
        rhi::Texture m_FallbackIrradianceMap;
        rhi::Texture m_FallbackPrefilteredEnvMap;
        rhi::Texture m_FallbackAo;
        glm::vec3    m_FallbackIblColor {0.0f};
    };
} // namespace vultra
