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
#include "vultra/function/rendering/srp/builtin/passes/bloom_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/debug_draw_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/final_composition_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/fxaa_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/gaussian_blur_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/geometry_warp_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/hzb_generate_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/pullpush_inpaint_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/selection_outline_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/ssao_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/ssr_composite_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/ssr_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/tone_mapping_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/ui_overlay_pass.hpp"
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

        void warnMissingPassInputOnce(std::string_view graphPass, std::string_view slot)
        {
            static std::unordered_set<std::string> warned;
            const auto                             key = std::string(graphPass) + ":" + std::string(slot);
            if (!warned.insert(key).second)
                return;

            VULTRA_CORE_ERROR("[DeclarativeRenderer] Render graph pass '{}' input '{}' is unavailable; skipping pass.",
                              graphPass,
                              slot);
        }

        class SsrCompositeBuiltin final : public IBuiltinRenderGraphPass
        {
        public:
            std::vector<BuiltinPassSpec> specs() const override
            {
                return {{"SsrComposite",
                         {"source", "reflection"},
                         {"color"},
                         {{.name = "enabled", .type = vrendergraph::ParamType::eBoolean, .defaultValue = true}}}};
            }

            void build(BuiltinPassHost& host,
                       std::string_view,
                       const vrendergraph::ParamBlock& params,
                       vrendergraph::PassBuildContext& passCtx) override
            {
                auto* ctx = host.currentBuildContext();
                if (!ctx)
                    return;
                if (!params.get<bool>("enabled", true))
                {
                    passCtx.setOutput("color", passCtx.getInput("source"));
                    return;
                }
                auto color = m_Pass.addPass(*ctx, passCtx.getInput("source"), passCtx.getInput("reflection"));
                if (color)
                {
                    ctx->data.set(kResKey_FinalCompositionSource, color);
                    if (ctx->view().stereoMode != StereoRenderMode::eMono)
                        ctx->data.set(kResKey_StereoColor, color);
                    passCtx.setOutput("color", color);
                }
            }

        private:
            SsrCompositePass m_Pass;
        };

        class HzbGenerateBuiltin final : public IBuiltinRenderGraphPass
        {
        public:
            std::vector<BuiltinPassSpec> specs() const override { return {{"HzbGenerate", {"depth"}, {"hzb"}, {}}}; }

            void build(BuiltinPassHost& host,
                       std::string_view,
                       const vrendergraph::ParamBlock&,
                       vrendergraph::PassBuildContext& passCtx) override
            {
                auto* ctx = host.currentBuildContext();
                if (!ctx)
                    return;
                m_Pass.addPass(*ctx, passCtx.getInput("depth"));
                if (auto hzb = ctx->data.tryGet(kResKey_HzbTexture))
                    passCtx.setOutput("hzb", hzb);
            }

        private:
            HzbGeneratePass m_Pass;
        };

        class SsaoBuiltin final : public IBuiltinRenderGraphPass
        {
        public:
            std::vector<BuiltinPassSpec> specs() const override
            {
                return {{"Ssao",
                         {"depth", "normal"},
                         {"ao"},
                         {
                             {.name = "enabled", .type = vrendergraph::ParamType::eBoolean, .defaultValue = false},
                             {.name = "maxRadiusPixels", .type = vrendergraph::ParamType::eInt, .defaultValue = 16},
                             {.name = "stepCount", .type = vrendergraph::ParamType::eInt, .defaultValue = 2},
                             {.name = "directionCount", .type = vrendergraph::ParamType::eInt, .defaultValue = 4},
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
                auto settings            = renderService->builtinRenderSettings().ssao;
                settings.enabled         = params.get<bool>("enabled", settings.enabled);
                settings.radius          = params.get<float>("radius", settings.radius);
                settings.bias            = params.get<float>("bias", settings.bias);
                settings.intensity       = params.get<float>("intensity", settings.intensity);
                settings.maxRadiusPixels = params.get<int>("maxRadiusPixels", settings.maxRadiusPixels);
                settings.stepCount       = params.get<int>("stepCount", settings.stepCount);
                settings.directionCount  = params.get<int>("directionCount", settings.directionCount);
                if (!settings.enabled)
                {
                    passCtx.setOutput("ao", {});
                    return;
                }
                auto ao = m_Pass.addPass(*ctx, passCtx.getInput("depth"), passCtx.getInput("normal"), settings);
                if (ao)
                {
                    ctx->data.set(kResKey_SsaoTexture, ao);
                    passCtx.setOutput("ao", ao);
                }
            }

        private:
            SsaoPass m_Pass;
        };

        class SsrBuiltin final : public IBuiltinRenderGraphPass
        {
        public:
            std::vector<BuiltinPassSpec> specs() const override
            {
                return {{"Ssr",
                         {"color", "depth", "normal", "material"},
                         {"reflection"},
                         {
                             {.name = "enabled", .type = vrendergraph::ParamType::eBoolean, .defaultValue = false},
                             {.name = "maxSteps", .type = vrendergraph::ParamType::eInt, .defaultValue = 8},
                             {.name = "binaryRefinement", .type = vrendergraph::ParamType::eInt, .defaultValue = 2},
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
                auto settings             = renderService->builtinRenderSettings().ssr;
                settings.enabled          = params.get<bool>("enabled", settings.enabled);
                settings.reflectionFactor = params.get<float>("reflectionFactor", settings.reflectionFactor);
                settings.maxSteps         = params.get<int>("maxSteps", settings.maxSteps);
                settings.binaryRefinement = params.get<int>("binaryRefinement", settings.binaryRefinement);
                settings.stride           = params.get<float>("stride", settings.stride);
                settings.thickness        = params.get<float>("thickness", settings.thickness);
                if (!settings.enabled)
                {
                    passCtx.setOutput("reflection", {});
                    return;
                }
                auto reflection = m_Pass.addPass(*ctx,
                                                 passCtx.getInput("color"),
                                                 passCtx.getInput("depth"),
                                                 passCtx.getInput("normal"),
                                                 passCtx.getInput("material"),
                                                 settings);
                if (reflection)
                {
                    ctx->data.set(kResKey_SsrTexture, reflection);
                    passCtx.setOutput("reflection", reflection);
                }
            }

        private:
            SsrPass m_Pass;
        };

        class FxaaBuiltin final : public IBuiltinRenderGraphPass
        {
        public:
            std::vector<BuiltinPassSpec> specs() const override
            {
                return {{"Fxaa",
                         {"source"},
                         {"color"},
                         {{.name = "enabled", .type = vrendergraph::ParamType::eBoolean, .defaultValue = true}}}};
            }

            void build(BuiltinPassHost& host,
                       std::string_view,
                       const vrendergraph::ParamBlock& params,
                       vrendergraph::PassBuildContext& passCtx) override
            {
                auto* ctx = host.currentBuildContext();
                if (!ctx)
                    return;
                if (!params.get<bool>("enabled", true))
                {
                    passCtx.setOutput("color", passCtx.getInput("source"));
                    return;
                }
                auto color = m_Pass.addPass(*ctx, passCtx.getInput("source"));
                if (color)
                {
                    ctx->data.set(kResKey_FinalCompositionSource, color);
                    passCtx.setOutput("color", color);
                }
            }

        private:
            FxaaPass m_Pass;
        };

        class GaussianBlurBuiltin final : public IBuiltinRenderGraphPass
        {
        public:
            std::vector<BuiltinPassSpec> specs() const override
            {
                return {{"GaussianBlur",
                         {"source"},
                         {"color"},
                         {
                             {.name = "scale", .type = vrendergraph::ParamType::eFloat, .defaultValue = 1.0f},
                             // direction: 0 = both (horizontal then vertical), 1 = horizontal only, 2 = vertical only
                             {.name = "direction", .type = vrendergraph::ParamType::eInt, .defaultValue = 0},
                         }}};
            }

            void build(BuiltinPassHost& host,
                       std::string_view,
                       const vrendergraph::ParamBlock& params,
                       vrendergraph::PassBuildContext& passCtx) override
            {
                auto* ctx = host.currentBuildContext();
                if (!ctx)
                    return;
                const auto         source    = passCtx.getInput("source");
                const float        scale     = params.get<float>("scale", 1.0f);
                const int          direction = params.get<int>("direction", 0);
                FrameGraphResource color {};
                if (direction == 1)
                    color = m_Pass.addPass(*ctx, source, scale, true);
                else if (direction == 2)
                    color = m_Pass.addPass(*ctx, source, scale, false);
                else
                    color = m_Pass.addPass(*ctx, source, scale);
                if (color)
                    passCtx.setOutput("color", color);
            }

        private:
            GaussianBlurPass m_Pass;
        };

        class BloomBuiltin final : public IBuiltinRenderGraphPass
        {
        public:
            std::vector<BuiltinPassSpec> specs() const override
            {
                return {{"Bloom",
                         {"source"},
                         {"color"},
                         {
                             {.name = "enabled", .type = vrendergraph::ParamType::eBoolean, .defaultValue = true},
                             {.name = "threshold", .type = vrendergraph::ParamType::eFloat, .defaultValue = 1.0f},
                             {.name = "knee", .type = vrendergraph::ParamType::eFloat, .defaultValue = 0.5f},
                             {.name = "intensity", .type = vrendergraph::ParamType::eFloat, .defaultValue = 0.6f},
                             {.name = "scale", .type = vrendergraph::ParamType::eFloat, .defaultValue = 1.0f},
                             {.name = "iterations", .type = vrendergraph::ParamType::eInt, .defaultValue = 1},
                         }}};
            }

            void build(BuiltinPassHost& host,
                       std::string_view,
                       const vrendergraph::ParamBlock& params,
                       vrendergraph::PassBuildContext& passCtx) override
            {
                auto* ctx = host.currentBuildContext();
                if (!ctx)
                    return;
                if (!params.get<bool>("enabled", true))
                {
                    passCtx.setOutput("color", passCtx.getInput("source"));
                    return;
                }
                auto color = m_Pass.addPass(*ctx,
                                            passCtx.getInput("source"),
                                            params.get<float>("threshold", 1.0f),
                                            params.get<float>("knee", 0.5f),
                                            params.get<float>("intensity", 0.6f),
                                            params.get<float>("scale", 1.0f),
                                            params.get<int>("iterations", 1));
                if (color)
                {
                    ctx->data.set(kResKey_FinalCompositionSource, color);
                    if (ctx->view().stereoMode != StereoRenderMode::eMono)
                        ctx->data.set(kResKey_StereoColor, color);
                    passCtx.setOutput("color", color);
                }
            }

        private:
            BloomPass m_Pass;
        };

        class ToneMappingBuiltin final : public IBuiltinRenderGraphPass
        {
        public:
            std::vector<BuiltinPassSpec> specs() const override
            {
                return {{"ToneMapping",
                         {"source"},
                         {"color"},
                         {{.name = "enabled", .type = vrendergraph::ParamType::eBoolean, .defaultValue = true}}}};
            }

            void build(BuiltinPassHost& host,
                       std::string_view,
                       const vrendergraph::ParamBlock& params,
                       vrendergraph::PassBuildContext& passCtx) override
            {
                auto* ctx = host.currentBuildContext();
                if (!ctx)
                    return;
                const bool enabled = params.get<bool>("enabled", true) && host.applyToneMappingThisFrame();
                if (!enabled)
                {
                    passCtx.setOutput("color", passCtx.getInput("source"));
                    return;
                }
                auto color = m_Pass.addPass(*ctx,
                                            passCtx.getInput("source"),
                                            params.get<float>("exposure", 1.0f),
                                            params.get<int>("method", 0));
                if (color)
                {
                    ctx->data.set(kResKey_FinalCompositionSource, color);
                    passCtx.setOutput("color", color);
                }
            }

        private:
            ToneMappingPass m_Pass;
        };

        class SelectionOutlineBuiltin final : public IBuiltinRenderGraphPass
        {
        public:
            std::vector<BuiltinPassSpec> specs() const override
            {
                return {{"SelectionOutline",
                         {"source", "entityId", "depth"},
                         {"color"},
                         {{.name = "enabled", .type = vrendergraph::ParamType::eBoolean, .defaultValue = true}}}};
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
                auto settings        = renderService->builtinRenderSettings().selectionOutline;
                settings.enabled     = params.get<bool>("enabled", settings.enabled);
                settings.thickness   = params.get<float>("thickness", settings.thickness);
                settings.fillOpacity = params.get<float>("fillOpacity", settings.fillOpacity);
                settings.edgeOpacity = params.get<float>("edgeOpacity", settings.edgeOpacity);
                const bool cameraAllowsOutline =
                    ctx->view().camera != nullptr && ctx->view().camera->selectionOutlineEnabled;
                if (!settings.enabled || settings.selectedEntityId == 0u || !cameraAllowsOutline ||
                    ctx->rd.getBackendApi() == rhi::RenderBackendApi::eWebGPU)
                {
                    passCtx.setOutput("color", passCtx.getInput("source"));
                    return;
                }
                auto color = m_Pass.addPass(*ctx,
                                            passCtx.getInput("source"),
                                            passCtx.getInput("entityId"),
                                            passCtx.getInput("depth"),
                                            settings);
                if (color)
                {
                    ctx->data.set(kResKey_FinalCompositionSource, color);
                    passCtx.setOutput("color", color);
                }
            }

        private:
            SelectionOutlinePass m_Pass;
        };

        class DebugDrawBuiltin final : public IBuiltinRenderGraphPass
        {
        public:
            std::vector<BuiltinPassSpec> specs() const override
            {
                return {{"DebugDraw",
                         {"source", "depth"},
                         {"color"},
                         {{.name = "enabled", .type = vrendergraph::ParamType::eBoolean, .defaultValue = true}}}};
            }

            void build(BuiltinPassHost& host,
                       std::string_view,
                       const vrendergraph::ParamBlock& params,
                       vrendergraph::PassBuildContext& passCtx) override
            {
                auto*       ctx                   = host.currentBuildContext();
                auto*       renderService         = host.renderService();
                const auto* camera                = ctx ? ctx->view().camera : nullptr;
                const bool  cameraAllowsDebugDraw = camera != nullptr && camera->debugDrawEnabled;
                if (!ctx || !renderService || !params.get<bool>("enabled", true) ||
                    !renderService->builtinRenderSettings().debugDraw.enabled || !cameraAllowsDebugDraw)
                {
                    passCtx.setOutput("color", passCtx.getInput("source"));
                    return;
                }
                // Match the scene geometry's clip space: GPUCameraBlock flips
                // projection[1][1] for Vulkan (see upload_resources.cpp). The debug-draw
                // VP must apply the same flip or wireframes drift in Y as the camera moves.
                glm::mat4 debugProjection = camera->projection;
                if (ctx->rd.getBackendApi() == rhi::RenderBackendApi::eVulkan)
                    debugProjection[1][1] *= -1.0f;
                auto color = m_Pass.addPass(
                    *ctx, passCtx.getInput("source"), passCtx.getInput("depth"), debugProjection * camera->view);
                if (color)
                {
                    ctx->data.set(kResKey_FinalCompositionSource, color);
                    passCtx.setOutput("color", color);
                }
                else
                {
                    passCtx.setOutput("color", passCtx.getInput("source"));
                }
            }

        private:
            DebugDrawPass m_Pass;
        };

        class UiOverlayBuiltin final : public IBuiltinRenderGraphPass
        {
        public:
            std::vector<BuiltinPassSpec> specs() const override
            {
                return {{"UiOverlay",
                         {"source"},
                         {"color"},
                         {{.name = "enabled", .type = vrendergraph::ParamType::eBoolean, .defaultValue = true}}}};
            }

            void build(BuiltinPassHost& host,
                       std::string_view,
                       const vrendergraph::ParamBlock& params,
                       vrendergraph::PassBuildContext& passCtx) override
            {
                auto* ctx = host.currentBuildContext();
                if (!ctx)
                    return;
                if (!params.get<bool>("enabled", true))
                {
                    passCtx.setOutput("color", passCtx.getInput("source"));
                    return;
                }
                // Pull scene depth from the frame data registry (published by the
                // depth/gbuffer pass) so world-space UI is occluded by geometry;
                // it is optional, so graphs without a depth pass just skip occlusion.
                const auto depth = ctx->data.tryGet(kResKey_DepthTexture);
                auto       color = m_Pass.addPass(*ctx, passCtx.getInput("source"), depth);
                if (color)
                {
                    ctx->data.set(kResKey_FinalCompositionSource, color);
                    passCtx.setOutput("color", color);
                }
                else
                {
                    passCtx.setOutput("color", passCtx.getInput("source"));
                }
            }

        private:
            UiOverlayPass m_Pass;
        };

        class GeometryWarpBuiltin final : public IBuiltinRenderGraphPass
        {
        public:
            std::vector<BuiltinPassSpec> specs() const override
            {
                return {{"GeometryWarp",
                         {"source", "depth"},
                         {"color"},
                         {
                             {.name = "enabled", .type = vrendergraph::ParamType::eBoolean, .defaultValue = true},
                             {.name = "sourceView", .type = vrendergraph::ParamType::eString, .defaultValue = "left"},
                             {.name = "targetView", .type = vrendergraph::ParamType::eString, .defaultValue = "right"},
                             {.name         = "gridSize",
                              .type         = vrendergraph::ParamType::eInt,
                              .defaultValue = 1,
                              .minValue     = 1,
                              .maxValue     = 16},
                             {.name         = "sideLenThreshold",
                              .type         = vrendergraph::ParamType::eFloat,
                              .defaultValue = 0.01f,
                              .minValue     = 0.0f,
                              .maxValue     = 0.5f},
                             {.name = "useDepthAware", .type = vrendergraph::ParamType::eBoolean, .defaultValue = true},
                         }}};
            }

            void build(BuiltinPassHost& host,
                       std::string_view,
                       const vrendergraph::ParamBlock& params,
                       vrendergraph::PassBuildContext& passCtx) override
            {
                auto* ctx = host.currentBuildContext();
                if (!ctx)
                    return;
                if (!params.get<bool>("enabled", true))
                {
                    passCtx.setOutput("color", passCtx.getInput("source"));
                    return;
                }

                ViewSynthesisSettings settings;
                settings.enabled    = true;
                settings.sourceView = params.get<std::string>("sourceView", settings.sourceView);
                settings.targetView = params.get<std::string>("targetView", settings.targetView);
                settings.gridSize   = static_cast<uint32_t>(std::max(params.get<int>("gridSize", 1), 1));
                settings.sideLenThreshold =
                    std::max(params.get<float>("sideLenThreshold", settings.sideLenThreshold), 0.0f);
                settings.useDepthAware = params.get<bool>("useDepthAware", settings.useDepthAware);
                auto color = m_Pass.addPass(*ctx, passCtx.getInput("source"), passCtx.getInput("depth"), settings);
                if (color)
                {
                    ctx->data.set(kResKey_FinalCompositionSource, color);
                    if (ctx->view().stereoMode != StereoRenderMode::eMono)
                        ctx->data.set(kResKey_StereoColor, color);
                    passCtx.setOutput("color", color);
                }
            }

        private:
            GeometryWarpPass m_Pass;
        };

        class PullpushInpaintBuiltin final : public IBuiltinRenderGraphPass
        {
        public:
            std::vector<BuiltinPassSpec> specs() const override
            {
                return {{"PullpushInpaint",
                         {"source"},
                         {"color"},
                         {
                             {.name = "enabled", .type = vrendergraph::ParamType::eBoolean, .defaultValue = true},
                             {.name = "useDepthAware", .type = vrendergraph::ParamType::eBoolean, .defaultValue = true},
                             {.name         = "depthThreshold",
                              .type         = vrendergraph::ParamType::eFloat,
                              .defaultValue = 0.01f,
                              .minValue     = 0.0f,
                              .maxValue     = 0.1f},
                         }}};
            }

            void build(BuiltinPassHost& host,
                       std::string_view,
                       const vrendergraph::ParamBlock& params,
                       vrendergraph::PassBuildContext& passCtx) override
            {
                auto* ctx = host.currentBuildContext();
                if (!ctx)
                    return;
                if (!params.get<bool>("enabled", true))
                {
                    passCtx.setOutput("color", passCtx.getInput("source"));
                    return;
                }

                ViewSynthesisSettings settings;
                settings.useDepthAware  = params.get<bool>("useDepthAware", settings.useDepthAware);
                settings.depthThreshold = std::max(params.get<float>("depthThreshold", settings.depthThreshold), 0.0f);
                auto color              = m_Pass.addPass(*ctx, passCtx.getInput("source"), settings);
                if (color)
                {
                    ctx->data.set(kResKey_FinalCompositionSource, color);
                    if (ctx->view().stereoMode != StereoRenderMode::eMono)
                        ctx->data.set(kResKey_StereoColor, color);
                    passCtx.setOutput("color", color);
                }
            }

        private:
            PullPushInpaintPass m_Pass;
        };

        class FinalCompositionBuiltin final : public IBuiltinRenderGraphPass
        {
        public:
            std::vector<BuiltinPassSpec> specs() const override
            {
                return {{"FinalComposition", {"source"}, {"target"}, {}}};
            }

            void build(BuiltinPassHost& host,
                       std::string_view,
                       const vrendergraph::ParamBlock&,
                       vrendergraph::PassBuildContext& passCtx) override
            {
                auto* ctx = host.currentBuildContext();
                if (!ctx || !ctx->view().target)
                    return;
                auto source = passCtx.getInput("source");
                if (!source)
                {
                    warnMissingPassInputOnce("FinalComposition", "source");
                    return;
                }
                const auto* outputRef = passCtx.getOutputRef("target");
                const auto  outputName =
                    outputRef && isBackbufferResource(outputRef->resource) ? outputRef->resource : "target";
                const auto outputSelector = outputRef ? outputRef->selector : nlohmann::json::object();
                auto       backbuffer     = importRenderGraphBackbuffer(
                    ctx->fg, ctx->view(), outputName, outputSelector, "VRenderGraphBackbuffer");
                if (!backbuffer)
                {
                    // No render target for this output (e.g. the right/synth eye when
                    // the XR session closed and the view fell back to mono). Skip the
                    // actual composite, but still satisfy the graph's output contract
                    // (vrendergraph requires every declared output slot to be produced)
                    // with a pass-through. This output is terminal, so it is unused.
                    passCtx.setOutput("target", source);
                    return;
                }
                ctx->data.set(kResKey_FinalCompositionSource, source);
                auto target = m_Pass.compose(*ctx, backbuffer);
                if (target)
                    passCtx.setOutput("target", target);
            }

        private:
            FinalCompositionPass m_Pass;
        };
    } // namespace

    void appendPostProcessBuiltinRenderGraphPasses(std::vector<std::unique_ptr<IBuiltinRenderGraphPass>>& passes)
    {
        passes.push_back(std::make_unique<HzbGenerateBuiltin>());
        passes.push_back(std::make_unique<SsaoBuiltin>());
        passes.push_back(std::make_unique<SsrBuiltin>());
        passes.push_back(std::make_unique<SsrCompositeBuiltin>());
        passes.push_back(std::make_unique<GaussianBlurBuiltin>());
        passes.push_back(std::make_unique<BloomBuiltin>());
        passes.push_back(std::make_unique<ToneMappingBuiltin>());
        passes.push_back(std::make_unique<FxaaBuiltin>());
        passes.push_back(std::make_unique<SelectionOutlineBuiltin>());
        passes.push_back(std::make_unique<DebugDrawBuiltin>());
        passes.push_back(std::make_unique<UiOverlayBuiltin>());
        passes.push_back(std::make_unique<GeometryWarpBuiltin>());
        passes.push_back(std::make_unique<PullpushInpaintBuiltin>());
        passes.push_back(std::make_unique<FinalCompositionBuiltin>());
    }
} // namespace vultra
