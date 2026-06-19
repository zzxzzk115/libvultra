#include "vultra/function/rendering/srp/builtin/builtin_pass_groups.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/rhi/structs/framebuffer_info.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"
#include "vultra/function/framegraph/framegraph_import.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/rendering/render_structs.hpp"
#include "vultra/function/rendering/srp/builtin/builtin_pass_host.hpp"
#include "vultra/function/rendering/srp/builtin/passes/deferred_lighting_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/depth_pre_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/direct_gbuffer_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/raytracing_primary_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/shadow_map_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/skybox_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/thin_gbuffer_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/visibility_buffer_pass.hpp"
#include "vultra/function/rendering/srp/builtin/render_graph_backbuffer.hpp"
#include "vultra/function/rendering/srp/builtin/render_graph_resource_names.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"
#include "vultra/function/rendering/srp/render_target_desc.hpp"
#include "vultra/function/services/render_service.hpp"
#include <fg/FrameGraph.hpp>
#include <nlohmann/json.hpp>
#include <vrendergraph/vrendergraph.hpp>

namespace vultra
{
    namespace
    {
        // ===== Self-registering builtin render-graph passes =====
        //
        // Each adapter OWNS the rhi pass object(s) it drives, declares its own node
        // port/param spec(s) in specs() (the single source of truth for slot names and
        // parameters), and holds its per-frame build body in build(). Owner state (live
        // build context, services, the per-frame tone-mapping flag) is reached through
        // BuiltinPassHost. The full catalog is composed by makeBuiltinRenderGraphPasses()
        // in builtin_render_graph_pass_factory.cpp from the append functions below.

        [[nodiscard]] const RenderLight* findPrimaryDirectionalLight(const RenderWorld* world)
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

        [[nodiscard]] const RenderLight* findPrimaryShadowDirectionalLight(const RenderWorld* world)
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

        class CameraClearBuiltin final : public IBuiltinRenderGraphPass
        {
        public:
            std::vector<BuiltinPassSpec> specs() const override { return {{"CameraClear", {}, {"color"}, {}}}; }

            void build(BuiltinPassHost& host,
                       std::string_view,
                       const vrendergraph::ParamBlock&,
                       vrendergraph::PassBuildContext& passCtx) override
            {
                auto* ctx = host.currentBuildContext();
                if (!ctx || !ctx->view().target)
                    return;

                struct PassData
                {
                    FrameGraphResource color;
                };

                const auto desc = makeRenderViewTextureDesc(ctx->view(), ctx->view().target->getPixelFormat());
                const auto data = ctx->fg.addCallbackPass<PassData>(
                    "CameraClearPass",
                    [desc](FrameGraph::Builder& builder, PassData& pd) {
                        PASS_SETUP_ZONE;
                        pd.color = builder.create<framegraph::FrameGraphTexture>("CameraClear", desc);
                        pd.color = builder.write(pd.color,
                                                 framegraph::Attachment {
                                                     .index       = 0,
                                                     .imageAspect = rhi::ImageAspect::eColor,
                                                     .clearValue  = framegraph::ClearValue::eOpaqueBlack,
                                                 });
                    },
                    [](const PassData&, FrameGraphPassResources&, void* ctxPtr) {
                        VULTRA_SCOPED_FRAMEGRAPH_EXEC_CONTEXT(rc, ctxPtr);
                        auto framebufferInfo = rc.framebufferInfo();
                        if (!framebufferInfo || framebufferInfo->colorAttachments.empty())
                            return;

                        auto& attachment      = framebufferInfo->colorAttachments[0];
                        attachment.clearValue = rc.view().camera ? rc.view().camera->clearValue : rc.view().clearValue;
                        attachment.loadOp     = rhi::AttachmentLoadOp::eClear;
                        rc.cb.beginRendering(*framebufferInfo).endRendering();
                    });

                ctx->data.set(kResKey_FinalCompositionSource, data.color);
                passCtx.setOutput("color", data.color);
            }
        };

        // One object backs three node types: the full gbuffer pass and the two
        // depth-only prepass aliases (DirectDepthPre / DepthPre), which reuse it.
        class DirectGBufferBuiltin final : public IBuiltinRenderGraphPass
        {
        public:
            std::vector<BuiltinPassSpec> specs() const override
            {
                return {
                    {"DirectGBuffer", {"depth"}, {"color", "depth", "normal", "material", "emissive", "entityId"}, {}},
                    {"DirectDepthPre", {}, {"depth"}, {}},
                    {"DepthPre", {}, {"depth"}, {}},
                };
            }

            void build(BuiltinPassHost& host,
                       std::string_view type,
                       const vrendergraph::ParamBlock&,
                       vrendergraph::PassBuildContext& passCtx) override
            {
                auto* ctx = host.currentBuildContext();
                if (!ctx)
                    return;

                if (type == "DirectGBuffer")
                {
                    auto color = m_Pass.addPass(*ctx, passCtx.getInput("depth"));
                    if (color)
                    {
                        passCtx.setOutput("color", color);
                        ctx->data.set(kResKey_FinalCompositionSource, color);
                        if (ctx->view().stereoMode != StereoRenderMode::eMono)
                            ctx->data.set(kResKey_StereoColor, color);
                    }
                    if (auto res = ctx->data.tryGet(kResKey_DepthTexture))
                    {
                        passCtx.setOutput("depth", res);
                        if (ctx->view().stereoMode != StereoRenderMode::eMono)
                            ctx->data.set(kResKey_StereoDepth, res);
                    }
                    if (auto res = ctx->data.tryGet(kResKey_GBufferNormal))
                        passCtx.setOutput("normal", res);
                    if (auto res = ctx->data.tryGet(kResKey_GBufferMaterial))
                        passCtx.setOutput("material", res);
                    if (auto res = ctx->data.tryGet(kResKey_GBufferEmissive))
                        passCtx.setOutput("emissive", res);
                    if (auto res = ctx->data.tryGet(kResKey_GBufferEntityId))
                        passCtx.setOutput("entityId", res);
                    else
                        passCtx.setOutput("entityId", {});
                    return;
                }

                // DirectDepthPre / DepthPre: depth-only prepass reusing the gbuffer pass.
                m_Pass.addDepthPrePass(*ctx);
                if (auto depth = ctx->data.tryGet(kResKey_DepthTexture))
                {
                    passCtx.setOutput("depth", depth);
                    if (ctx->view().stereoMode != StereoRenderMode::eMono)
                        ctx->data.set(kResKey_StereoDepth, depth);
                }
            }

        private:
            DirectGBufferPass m_Pass;
        };

        class ShadowMapBuiltin final : public IBuiltinRenderGraphPass
        {
        public:
            std::vector<BuiltinPassSpec> specs() const override
            {
                return {{"ShadowMap",
                         {},
                         {"shadowMap", "shadowData"},
                         {
                             {.name = "enabled", .type = vrendergraph::ParamType::eBoolean, .defaultValue = true},
                             {.name = "resolution", .type = vrendergraph::ParamType::eInt, .defaultValue = 2048},
                             {.name = "cascadeCount", .type = vrendergraph::ParamType::eInt, .defaultValue = 4},
                             {.name = "coverageRadius", .type = vrendergraph::ParamType::eFloat, .defaultValue = 75.0f},
                             {.name = "lightDistance", .type = vrendergraph::ParamType::eFloat, .defaultValue = 120.0f},
                             {.name = "zRange", .type = vrendergraph::ParamType::eFloat, .defaultValue = 120.0f},
                             {.name = "splitLambda", .type = vrendergraph::ParamType::eFloat, .defaultValue = 0.60f},
                             {.name = "autoFitBounds", .type = vrendergraph::ParamType::eBoolean, .defaultValue = true},
                             {.name         = "stableTexelSnapping",
                              .type         = vrendergraph::ParamType::eBoolean,
                              .defaultValue = true},
                             {.name = "depthBias", .type = vrendergraph::ParamType::eFloat, .defaultValue = 0.0012f},
                             {.name = "normalBias", .type = vrendergraph::ParamType::eFloat, .defaultValue = 0.015f},
                             {.name = "pcssLightRadius", .type = vrendergraph::ParamType::eFloat, .defaultValue = 1.5f},
                         }}};
            }

            void build(BuiltinPassHost& host,
                       std::string_view,
                       const vrendergraph::ParamBlock& params,
                       vrendergraph::PassBuildContext& passCtx) override
            {
                auto* ctx           = host.currentBuildContext();
                auto* renderService = host.renderService();
                if (!ctx || !renderService)
                    return;
                auto settings    = renderService->builtinRenderSettings().shadow;
                settings.enabled = params.get<bool>("enabled", settings.enabled);
                settings.resolution =
                    static_cast<uint32_t>(params.get<int>("resolution", static_cast<int>(settings.resolution)));
                settings.cascadeCount =
                    static_cast<uint32_t>(params.get<int>("cascadeCount", static_cast<int>(settings.cascadeCount)));
                settings.coverageRadius      = params.get<float>("coverageRadius", settings.coverageRadius);
                settings.lightDistance       = params.get<float>("lightDistance", settings.lightDistance);
                settings.zRange              = params.get<float>("zRange", settings.zRange);
                settings.splitLambda         = params.get<float>("splitLambda", settings.splitLambda);
                settings.autoFitBounds       = params.get<bool>("autoFitBounds", settings.autoFitBounds);
                settings.stableTexelSnapping = params.get<bool>("stableTexelSnapping", settings.stableTexelSnapping);
                settings.depthBias           = params.get<float>("depthBias", settings.depthBias);
                settings.normalBias          = params.get<float>("normalBias", settings.normalBias);
                settings.pcssLightRadius     = params.get<float>("pcssLightRadius", settings.pcssLightRadius);
                const auto* shadowDirectionalLight = findPrimaryShadowDirectionalLight(ctx->view().renderWorld);
                settings.enabled                   = settings.enabled && shadowDirectionalLight != nullptr;
                if (shadowDirectionalLight)
                {
                    settings.lightDirection = shadowDirectionalLight->direction;
                }
                auto shadow = m_Pass.addPass(*ctx, settings);
                if (shadow.shadowMap)
                    passCtx.setOutput("shadowMap", shadow.shadowMap);
                if (shadow.shadowData)
                    passCtx.setOutput("shadowData", shadow.shadowData);
            }

        private:
            ShadowMapPass m_Pass;
        };

        // One node, two collaborators: deferred lighting then (optionally) the skybox,
        // which reads the lighting pass's environment cubemap.
        class DeferredLightingBuiltin final : public IBuiltinRenderGraphPass
        {
        public:
            std::vector<BuiltinPassSpec> specs() const override
            {
                return {{"DeferredLighting",
                         {"color", "normal", "material", "emissive", "depth", "ao", "shadowMap", "shadowData"},
                         {"color"},
                         {}}};
            }

            void build(BuiltinPassHost& host,
                       std::string_view,
                       const vrendergraph::ParamBlock& params,
                       vrendergraph::PassBuildContext& passCtx) override
            {
                auto* ctx           = host.currentBuildContext();
                auto* renderService = host.renderService();
                if (!ctx || !renderService)
                    return;
                const auto& settings             = renderService->builtinRenderSettings();
                auto        lightingSettings     = settings.pbrLighting;
                auto        shadowSettings       = settings.shadow;
                const bool  suppressCameraSkybox = ctx->view().camera != nullptr && ctx->view().camera->suppressSkybox;
                rhi::Texture* skyboxTexture      = !suppressCameraSkybox && settings.pbrLighting.showSkybox ?
                                                       settings.pbrLighting.environmentMap :
                                                       nullptr;
                lightingSettings.ambientIntensity =
                    params.get<float>("ambientIntensity", lightingSettings.ambientIntensity);
                lightingSettings.shadowStrength = params.get<float>("shadowStrength", lightingSettings.shadowStrength);
                lightingSettings.iblIntensity   = params.get<float>("iblIntensity", lightingSettings.iblIntensity);
                lightingSettings.debugViewMode  = static_cast<PbrLightingSettings::DebugViewMode>(std::clamp(
                    params.get<int>("debugViewMode", static_cast<int>(lightingSettings.debugViewMode)), 0, 6));
                shadowSettings.filterMode       = static_cast<ShadowRenderSettings::FilterMode>(
                    std::clamp(params.get<int>("shadowFilterMode", static_cast<int>(shadowSettings.filterMode)), 0, 2));
                shadowSettings.debugMode = static_cast<ShadowRenderSettings::DebugMode>(
                    std::clamp(params.get<int>("shadowDebugMode", static_cast<int>(shadowSettings.debugMode)), 0, 5));
                if (params.get<bool>("debugCascades", false))
                    shadowSettings.debugMode = ShadowRenderSettings::DebugMode::eCascade;
                const auto* primaryDirectionalLight = findPrimaryDirectionalLight(ctx->view().renderWorld);
                const auto* shadowDirectionalLight  = findPrimaryShadowDirectionalLight(ctx->view().renderWorld);
                if (primaryDirectionalLight)
                {
                    lightingSettings.directionalLightDirection = primaryDirectionalLight->direction;
                    lightingSettings.directionalLightColor     = primaryDirectionalLight->color;
                    lightingSettings.directionalLightIntensity = primaryDirectionalLight->intensity;
                }
                else if (ctx->view().renderWorld && !ctx->view().renderWorld->lights.empty())
                {
                    lightingSettings.directionalLightIntensity = 0.0f;
                }
                shadowSettings.enabled = shadowSettings.enabled && shadowDirectionalLight != nullptr;
                if (shadowDirectionalLight)
                    shadowSettings.lightDirection = shadowDirectionalLight->direction;
                const auto* renderEnvironment = ctx->view().renderWorld && ctx->view().renderWorld->environment.active ?
                                                    &ctx->view().renderWorld->environment :
                                                    nullptr;
                if (renderEnvironment)
                {
                    lightingSettings.ambientColor     = renderEnvironment->ambientColor;
                    lightingSettings.ambientIntensity = renderEnvironment->ambientIntensity;
                    lightingSettings.enableIBL        = renderEnvironment->enableIBL;
                    lightingSettings.iblColor         = renderEnvironment->iblColor;
                    lightingSettings.iblIntensity     = renderEnvironment->iblIntensity;
                    lightingSettings.environmentMap   = renderEnvironment->skybox;
                    skyboxTexture                     = suppressCameraSkybox ? nullptr : renderEnvironment->skybox;
                }
                if (const auto* probe = selectReflectionProbe(ctx->view().renderWorld, ctx->view().camera))
                {
                    lightingSettings.enableIBL    = probe->enableIBL;
                    lightingSettings.iblIntensity = probe->intensity;
                    if (probe->enableIBL)
                        lightingSettings.environmentMap = probe->environmentMap;
                }
                if (lightingSettings.debugViewMode != PbrLightingSettings::DebugViewMode::eLit ||
                    shadowSettings.debugMode != ShadowRenderSettings::DebugMode::eOff)
                    host.setApplyToneMappingThisFrame(false);
                shadowSettings.pcssBlockerSamples =
                    params.get<int>("pcssBlockerSamples", shadowSettings.pcssBlockerSamples);
                shadowSettings.pcssFilterSamples = params.get<int>("pcfRadius", shadowSettings.pcssFilterSamples);
                auto color                       = m_Lighting.addPass(*ctx,
                                                                      passCtx.getInput("color"),
                                                                      passCtx.getInput("normal"),
                                                                      passCtx.getInput("material"),
                                                                      passCtx.getInput("emissive"),
                                                                      passCtx.getInput("depth"),
                                                                      passCtx.getInput("ao"),
                                                                      passCtx.getInput("shadowMap"),
                                                                      passCtx.getInput("shadowData"),
                                                                      shadowSettings,
                                                                      lightingSettings,
                                                                      ctx->view().renderWorld);
                if (color)
                {
                    const bool cameraWantsSkybox =
                        !suppressCameraSkybox && ((ctx->view().camera && ctx->view().camera->clearMode == 1u) ||
                                                  settings.pbrLighting.showSkybox);
                    if (cameraWantsSkybox && skyboxTexture && ctx->data.contains(kResKey_DepthTexture))
                    {
                        const auto env = framegraph::importTexture(ctx->fg, "Environment Map", skyboxTexture);
                        color          = m_Skybox.addPass(*ctx,
                                                          color,
                                                          ctx->data.get(kResKey_DepthTexture),
                                                          env,
                                                          lightingSettings.environmentMap == skyboxTexture ?
                                                              m_Lighting.environmentCubemap() :
                                                              nullptr);
                    }
                    ctx->data.set(kResKey_FinalCompositionSource, color);
                    if (ctx->view().stereoMode != StereoRenderMode::eMono)
                        ctx->data.set(kResKey_StereoColor, color);
                    passCtx.setOutput("color", color);
                }
            }

        private:
            DeferredLightingPass m_Lighting;
            SkyboxPass           m_Skybox;
        };

        class RayTracingPrimaryBuiltin final : public IBuiltinRenderGraphPass
        {
        public:
            std::vector<BuiltinPassSpec> specs() const override { return {{"RayTracingPrimary", {}, {"color"}, {}}}; }

            void build(BuiltinPassHost& host,
                       std::string_view,
                       const vrendergraph::ParamBlock&,
                       vrendergraph::PassBuildContext& passCtx) override
            {
                auto* ctx = host.currentBuildContext();
                if (!ctx)
                    return;
                auto color = m_Pass.addPass(*ctx);
                if (color)
                {
                    ctx->data.set(kResKey_FinalCompositionSource, color);
                    passCtx.setOutput("color", color);
                }
            }

        private:
            RayTracingPrimaryPass m_Pass;
        };

        class VisibilityBufferBuiltin final : public IBuiltinRenderGraphPass
        {
        public:
            std::vector<BuiltinPassSpec> specs() const override
            {
                return {{"VisibilityBuffer", {}, {"visibility", "depth"}, {}}};
            }

            void build(BuiltinPassHost& host,
                       std::string_view,
                       const vrendergraph::ParamBlock&,
                       vrendergraph::PassBuildContext& passCtx) override
            {
                auto* ctx = host.currentBuildContext();
                if (!ctx)
                    return;
                auto visibility = m_Pass.addPass(*ctx);
                if (visibility)
                    passCtx.setOutput("visibility", visibility);
                if (auto depth = ctx->data.tryGet(kResKey_DepthTexture))
                    passCtx.setOutput("depth", depth);
            }

        private:
            VisibilityBufferPass m_Pass;
        };

        class ThinGBufferBuiltin final : public IBuiltinRenderGraphPass
        {
        public:
            std::vector<BuiltinPassSpec> specs() const override
            {
                return {{"ThinGBuffer",
                         {"visibility", "depth"},
                         {"color", "normal", "material", "emissive", "depth", "entityId"},
                         {}}};
            }

            void build(BuiltinPassHost& host,
                       std::string_view,
                       const vrendergraph::ParamBlock&,
                       vrendergraph::PassBuildContext& passCtx) override
            {
                auto* ctx = host.currentBuildContext();
                if (!ctx)
                    return;
                auto color = m_Pass.addPass(*ctx, passCtx.getInput("visibility"));
                if (color)
                    passCtx.setOutput("color", color);
                if (auto res = ctx->data.tryGet(kResKey_GBufferNormal))
                    passCtx.setOutput("normal", res);
                if (auto res = ctx->data.tryGet(kResKey_GBufferMaterial))
                    passCtx.setOutput("material", res);
                if (auto res = ctx->data.tryGet(kResKey_GBufferEmissive))
                    passCtx.setOutput("emissive", res);
                passCtx.setOutput("depth", passCtx.getInput("depth"));
                if (auto res = ctx->data.tryGet(kResKey_GBufferEntityId))
                    passCtx.setOutput("entityId", res);
                else
                    passCtx.setOutput("entityId", {});
            }

        private:
            ThinGBufferPass m_Pass;
        };
    } // namespace

    void appendSceneBuiltinRenderGraphPasses(std::vector<std::unique_ptr<IBuiltinRenderGraphPass>>& passes)
    {
        passes.push_back(std::make_unique<CameraClearBuiltin>());
        passes.push_back(std::make_unique<DirectGBufferBuiltin>());
        passes.push_back(std::make_unique<ShadowMapBuiltin>());
        passes.push_back(std::make_unique<DeferredLightingBuiltin>());
        passes.push_back(std::make_unique<RayTracingPrimaryBuiltin>());
        passes.push_back(std::make_unique<VisibilityBufferBuiltin>());
        passes.push_back(std::make_unique<ThinGBufferBuiltin>());
    }
} // namespace vultra
