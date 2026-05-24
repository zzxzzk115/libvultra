#include "vultra/function/rendering/srp/builtin/features/builtin_screen_space_feature.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"
#include "vultra/function/rendering/srp/builtin/passes/fxaa_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/ssao_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/selection_outline_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/ssr_pass.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"
#include "vultra/function/services/render_service.hpp"

#include <vultra/core/rhi/structs/render_backend_api.hpp>

namespace vultra
{
    BuiltinScreenSpaceFeature::BuiltinScreenSpaceFeature(IRenderService& renderService) : m_RenderService(renderService)
    {
        m_SsaoPass = new SsaoPass();
        m_SsrPass  = new SsrPass();
        m_SelectionOutlinePass = new SelectionOutlinePass();
        m_FxaaPass = new FxaaPass();
    }

    BuiltinScreenSpaceFeature::~BuiltinScreenSpaceFeature()
    {
        delete m_SsaoPass;
        delete m_SsrPass;
        delete m_SelectionOutlinePass;
        delete m_FxaaPass;
    }

    void BuiltinScreenSpaceFeature::addPasses(FrameGraphBuildContext& ctx)
    {
        const auto& settings = m_RenderService.builtinRenderSettings();

        const bool hasDepth  = ctx.data.contains(kResKey_DepthTexture);
        const bool hasNormal = ctx.data.contains(kResKey_GBufferNormal);
        const bool hasMrAo   = ctx.data.contains(kResKey_GBufferMetallicRoughnessAO);
        const bool hasColor  = ctx.data.contains(kResKey_FinalCompositionSource);
        const bool hasEntityId = ctx.data.contains(kResKey_GBufferEntityId);

        // The pass implementations are intentionally gated on SRP resources instead of legacy mesh/texture systems.
        // SSAO requires depth + normal; SSR requires depth + normal + material MR/AO + current scene color.
        if (settings.ssao.enabled && hasDepth && hasNormal)
        {
            auto ao = m_SsaoPass->addPass(
                ctx, ctx.data.get(kResKey_DepthTexture), ctx.data.get(kResKey_GBufferNormal), settings.ssao);
            ctx.data.set(kResKey_SsaoTexture, ao);
        }

        if (settings.ssr.enabled && hasDepth && hasNormal && hasMrAo && hasColor)
        {
            auto reflection = m_SsrPass->addPass(ctx,
                                                 ctx.data.get(kResKey_FinalCompositionSource),
                                                 ctx.data.get(kResKey_DepthTexture),
                                                 ctx.data.get(kResKey_GBufferNormal),
                                                 ctx.data.get(kResKey_GBufferMetallicRoughnessAO),
                                                 settings.ssr);
            ctx.data.set(kResKey_SsrTexture, reflection);
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
    }
} // namespace vultra
