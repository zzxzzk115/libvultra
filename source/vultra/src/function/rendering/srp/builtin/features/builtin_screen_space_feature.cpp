#include "vultra/function/rendering/srp/builtin/features/builtin_screen_space_feature.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"
#include "vultra/function/rendering/srp/builtin/passes/fxaa_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/selection_outline_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/ssr_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/ssr_composite_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/tone_mapping_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/ui_overlay_pass.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"
#include "vultra/function/services/render_service.hpp"

#include <vultra/core/rhi/structs/render_backend_api.hpp>

namespace vultra
{
    BuiltinScreenSpaceFeature::BuiltinScreenSpaceFeature(IRenderService& renderService) : m_RenderService(renderService)
    {
        m_SsrPass              = std::make_unique<SsrPass>();
        m_SsrCompositePass     = std::make_unique<SsrCompositePass>();
        m_ToneMappingPass      = std::make_unique<ToneMappingPass>();
        m_SelectionOutlinePass = std::make_unique<SelectionOutlinePass>();
        m_FxaaPass             = std::make_unique<FxaaPass>();
        m_UiOverlayPass        = std::make_unique<UiOverlayPass>();
    }

    BuiltinScreenSpaceFeature::~BuiltinScreenSpaceFeature() = default;

    void BuiltinScreenSpaceFeature::addPasses(FrameGraphBuildContext& ctx)
    {
        const auto& settings = m_RenderService.builtinRenderSettings();

        const bool hasDepth  = ctx.data.contains(kResKey_DepthTexture);
        const bool hasNormal = ctx.data.contains(kResKey_GBufferNormal);
        const bool hasMrAo   = ctx.data.contains(kResKey_GBufferMaterial);
        const bool hasColor  = ctx.data.contains(kResKey_FinalCompositionSource);
        const bool hasEntityId = ctx.data.contains(kResKey_GBufferEntityId);

        // The pass implementations are intentionally gated on SRP resources instead of legacy mesh/texture systems.
        // SSR requires depth + normal + material MR/AO + current scene color.
        if (settings.ssr.enabled && hasDepth && hasNormal && hasMrAo && hasColor)
        {
            auto reflection = m_SsrPass->addPass(ctx,
                                                 ctx.data.get(kResKey_FinalCompositionSource),
                                                 ctx.data.get(kResKey_DepthTexture),
                                                 ctx.data.get(kResKey_GBufferNormal),
                                                 ctx.data.get(kResKey_GBufferMaterial),
                                                 settings.ssr);
            ctx.data.set(kResKey_SsrTexture, reflection);
            if (reflection)
            {
                auto composited = m_SsrCompositePass->addPass(ctx, ctx.data.get(kResKey_FinalCompositionSource), reflection);
                if (composited)
                    ctx.data.set(kResKey_FinalCompositionSource, composited);
            }
        }

        const bool shouldToneMap = settings.pbrLighting.debugViewMode == PbrLightingSettings::DebugViewMode::eLit &&
                                   settings.shadow.debugMode == ShadowRenderSettings::DebugMode::eOff;
        if (settings.toneMapping.enabled && shouldToneMap && hasColor)
        {
            auto toneMapped = m_ToneMappingPass->addPass(ctx,
                                                         ctx.data.get(kResKey_FinalCompositionSource),
                                                         settings.toneMapping.exposure,
                                                         settings.toneMapping.method);
            if (toneMapped)
                ctx.data.set(kResKey_FinalCompositionSource, toneMapped);
        }

        if (settings.enableFXAA && hasColor)
        {
            auto aaColor = m_FxaaPass->addPass(ctx, ctx.data.get(kResKey_FinalCompositionSource));
            ctx.data.set(kResKey_FinalCompositionSource, aaColor);
        }

        const bool cameraAllowsOutline = ctx.view().camera != nullptr && ctx.view().camera->selectionOutlineEnabled;
        if (settings.selectionOutline.enabled && settings.selectionOutline.selectedEntityId != 0u &&
            cameraAllowsOutline && hasColor && hasEntityId && hasDepth &&
            ctx.rd.getBackendApi() != rhi::RenderBackendApi::eWebGPU)
        {
            auto outlined = m_SelectionOutlinePass->addPass(ctx,
                                                            ctx.data.get(kResKey_FinalCompositionSource),
                                                            ctx.data.get(kResKey_GBufferEntityId),
                                                            ctx.data.get(kResKey_DepthTexture),
                                                            settings.selectionOutline);
            ctx.data.set(kResKey_SelectionOutlineOutput, outlined);
            ctx.data.set(kResKey_FinalCompositionSource, outlined);
        }

        if (ctx.data.contains(kResKey_FinalCompositionSource))
        {
            auto uiComposited = m_UiOverlayPass->addPass(ctx, ctx.data.get(kResKey_FinalCompositionSource));
            if (uiComposited)
                ctx.data.set(kResKey_FinalCompositionSource, uiComposited);
        }
    }
} // namespace vultra
