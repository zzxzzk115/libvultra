#include "vultra/function/rendering/srp/builtin/features/direct_gbuffer_feature.hpp"

#include "vultra/function/framegraph/framegraph_context.hpp"
#include "vultra/function/rendering/srp/builtin/passes/deferred_lighting_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/direct_gbuffer_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/shadow_map_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/ssao_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/skybox_pass.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"
#include "vultra/function/services/render_service.hpp"
#include "vultra/function/framegraph/framegraph_import.hpp"

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
        m_GBufferPass  = std::make_unique<DirectGBufferPass>();
        m_ShadowPass   = std::make_unique<ShadowMapPass>();
        m_SsaoPass     = std::make_unique<SsaoPass>();
        m_LightingPass = std::make_unique<DeferredLightingPass>();
        m_SkyboxPass   = std::make_unique<SkyboxPass>();
    }

    DirectGBufferFeature::~DirectGBufferFeature() = default;

    void DirectGBufferFeature::addPasses(FrameGraphBuildContext& ctx)
    {
        const bool hasMeshInstances = ctx.view().renderWorld != nullptr && !ctx.view().renderWorld->instances.empty();
        if (!hasMeshInstances || !ctx.view().target)
            return;

        m_GBufferPass->addDepthPrePass(ctx);
        const auto prepassDepth = ctx.data.tryGet(kResKey_DepthTexture);
        auto color = m_GBufferPass->addPass(ctx, prepassDepth);
        if (!color || !ctx.data.contains(kResKey_DepthTexture) || !ctx.data.contains(kResKey_GBufferNormal) ||
            !ctx.data.contains(kResKey_GBufferMaterial) || !ctx.data.contains(kResKey_GBufferEmissive))
            return;

        const auto& settings = m_RenderService.builtinRenderSettings();
        auto shadowSettings = settings.shadow;
        auto lightingSettings = settings.pbrLighting;
        const bool suppressCameraSkybox = ctx.view().camera != nullptr && ctx.view().camera->suppressSkybox;
        rhi::Texture* skyboxTexture =
            !suppressCameraSkybox && settings.pbrLighting.showSkybox ? settings.pbrLighting.environmentMap : nullptr;
        const auto* renderEnvironment =
            ctx.view().renderWorld && ctx.view().renderWorld->environment.active ?
                &ctx.view().renderWorld->environment :
                nullptr;
        if (renderEnvironment)
        {
            lightingSettings.ambientColor = renderEnvironment->ambientColor;
            lightingSettings.ambientIntensity = renderEnvironment->ambientIntensity;
            lightingSettings.enableIBL = renderEnvironment->enableIBL;
            lightingSettings.iblColor = renderEnvironment->iblColor;
            lightingSettings.iblIntensity = renderEnvironment->iblIntensity;
            lightingSettings.environmentMap = renderEnvironment->skybox;
            skyboxTexture = suppressCameraSkybox ? nullptr : renderEnvironment->skybox;
        }
        if (const auto* probe = selectReflectionProbe(ctx.view().renderWorld, ctx.view().camera))
        {
            lightingSettings.enableIBL = probe->enableIBL;
            lightingSettings.iblIntensity = probe->intensity;
            if (probe->enableIBL)
                lightingSettings.environmentMap = probe->environmentMap;
        }
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

        FrameGraphResource ssao = 0;
        if (settings.ssao.enabled)
        {
            ssao = m_SsaoPass->addPass(ctx,
                                       ctx.data.get(kResKey_DepthTexture),
                                       ctx.data.get(kResKey_GBufferNormal),
                                       settings.ssao);
            if (ssao)
                ctx.data.set(kResKey_SsaoTexture, ssao);
        }

        auto shadow = m_ShadowPass->addPass(ctx, shadowSettings);

        auto lit = m_LightingPass->addPass(ctx,
                                           color,
                                           ctx.data.get(kResKey_GBufferNormal),
                                           ctx.data.get(kResKey_GBufferMaterial),
                                           ctx.data.get(kResKey_GBufferEmissive),
                                           ctx.data.get(kResKey_DepthTexture),
                                           ssao,
                                           shadow.shadowMap,
                                           shadow.shadowData,
                                           shadowSettings,
                                           lightingSettings,
                                           ctx.view().renderWorld);
        if (lit)
        {
            const bool cameraWantsSkybox =
                !suppressCameraSkybox &&
                ((ctx.view().camera && ctx.view().camera->clearMode == 1u) ||
                 settings.pbrLighting.showSkybox);
            if (cameraWantsSkybox && skyboxTexture &&
                ctx.data.contains(kResKey_DepthTexture))
            {
                const auto env = framegraph::importTexture(ctx.fg,
                                                           "Environment Map",
                                                           skyboxTexture);
                lit = m_SkyboxPass->addPass(ctx,
                                            lit,
                                            ctx.data.get(kResKey_DepthTexture),
                                            env,
                                            lightingSettings.environmentMap == skyboxTexture ?
                                                m_LightingPass->environmentCubemap() :
                                                nullptr);
            }
            ctx.data.set(kResKey_FinalCompositionSource, lit);
        }
    }
} // namespace vultra
