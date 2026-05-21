#include "vultra/function/rendering/srp/builtin/features/direct_gbuffer_feature.hpp"

#include "vultra/function/framegraph/framegraph_context.hpp"
#include "vultra/function/rendering/srp/builtin/passes/deferred_lighting_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/direct_gbuffer_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/shadow_map_pass.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"
#include "vultra/function/services/render_service.hpp"

namespace vultra
{
    namespace
    {
        const RenderLight* findPrimaryDirectionalLight(const RenderWorld* world)
        {
            if (!world)
                return nullptr;
            for (const auto& light : world->lights)
            {
                if (light.kind == RenderLightKind::eDirectional)
                    return &light;
            }
            return nullptr;
        }

        const RenderLight* findPrimaryShadowDirectionalLight(const RenderWorld* world)
        {
            if (!world)
                return nullptr;
            for (const auto& light : world->lights)
            {
                if (light.kind == RenderLightKind::eDirectional && light.castsShadow)
                    return &light;
            }
            return nullptr;
        }
    } // namespace

    DirectGBufferFeature::DirectGBufferFeature(IRenderService& renderService) : m_RenderService(renderService)
    {
        m_GBufferPass    = new DirectGBufferPass();
        m_ShadowPass     = new ShadowMapPass();
        m_LightingPass   = new DeferredLightingPass();
    }

    DirectGBufferFeature::~DirectGBufferFeature()
    {
        delete m_GBufferPass;
        delete m_ShadowPass;
        delete m_LightingPass;
    }

    void DirectGBufferFeature::addPasses(FrameGraphBuildContext& ctx)
    {
        const bool hasMeshInstances = ctx.view().renderWorld != nullptr && !ctx.view().renderWorld->instances.empty();
        if (!hasMeshInstances || !ctx.view().target)
            return;

        auto color = m_GBufferPass->addPass(ctx);
        if (!color || !ctx.data.contains(kResKey_DepthTexture) || !ctx.data.contains(kResKey_GBufferNormal) ||
            !ctx.data.contains(kResKey_GBufferMetallicRoughnessAO))
            return;

        const auto& settings = m_RenderService.builtinRenderSettings();
        auto shadowSettings = settings.shadow;
        auto lightingSettings = settings.pbrLighting;
        const auto* primaryDirectionalLight = findPrimaryDirectionalLight(ctx.view().renderWorld);
        const auto* shadowDirectionalLight  = findPrimaryShadowDirectionalLight(ctx.view().renderWorld);
        if (primaryDirectionalLight)
        {
            lightingSettings.directionalLightDirection = primaryDirectionalLight->direction;
            lightingSettings.directionalLightColor = primaryDirectionalLight->color;
            lightingSettings.directionalLightIntensity = primaryDirectionalLight->intensity;
        }
        else if (ctx.view().renderWorld && !ctx.view().renderWorld->lights.empty())
        {
            lightingSettings.directionalLightIntensity = 0.0f;
        }

        shadowSettings.enabled = settings.shadow.enabled && shadowDirectionalLight != nullptr;
        if (shadowDirectionalLight)
            shadowSettings.lightDirection = shadowDirectionalLight->direction;

        auto shadow = m_ShadowPass->addPass(ctx, shadowSettings);

        auto lit = m_LightingPass->addPass(ctx,
                                           color,
                                           ctx.data.get(kResKey_GBufferNormal),
                                           ctx.data.get(kResKey_GBufferMetallicRoughnessAO),
                                           ctx.data.get(kResKey_DepthTexture),
                                           shadow.shadowMap,
                                           shadow.shadowData,
                                           shadowSettings,
                                           lightingSettings,
                                           ctx.view().renderWorld);
        if (lit)
            ctx.data.set(kResKey_FinalCompositionSource, lit);
    }
} // namespace vultra
