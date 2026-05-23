#include "vultra/function/rendering/srp/declarative_renderer.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/rhi/structs/framebuffer_info.hpp"
#include "vultra/core/rhi/structs/render_backend_api.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"
#include "vultra/function/framegraph/framegraph_import.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/rendering/srp/builtin/features/builtin_screen_space_feature.hpp"
#include "vultra/function/rendering/srp/builtin/features/compatibility_basecolor_feature.hpp"
#include "vultra/function/rendering/srp/builtin/features/direct_gbuffer_feature.hpp"
#include "vultra/function/rendering/srp/builtin/features/final_composition_feature.hpp"
#include "vultra/function/rendering/srp/builtin/features/general_gaussian_splat_feature.hpp"
#include "vultra/function/rendering/srp/builtin/features/meshlet_feature.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"
#include "vultra/function/services/asset_service.hpp"
#include "vultra/function/services/render_service.hpp"
#include "vultra/function/services/shader_service.hpp"

#include <fg/FrameGraph.hpp>
#include <nlohmann/json.hpp>
#include <vrendergraph/vrendergraph.hpp>

#include <vbase/core/hash.hpp>

#include <algorithm>
#include <cctype>
#include <optional>
#include <unordered_map>
#include <utility>

namespace vultra
{
    namespace
    {
        [[nodiscard]] std::string getString(sol::table table, const char* key, std::string fallback = {})
        {
            sol::object value = table[key];
            return value.is<std::string>() ? value.as<std::string>() : std::move(fallback);
        }

        [[nodiscard]] std::string normalizeId(std::string value)
        {
            for (auto& ch : value)
            {
                if (ch == '-' || ch == ' ')
                    ch = '_';
                else
                    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }
            return value;
        }

        [[nodiscard]] FrameGraphResourceKey resourceKeyFor(std::string_view name)
        {
            const auto normalized = normalizeId(std::string(name));
            if (normalized == "final_composition_source" || normalized == "color" || normalized == "camera_color")
                return kResKey_FinalCompositionSource;
            if (normalized == "depth" || normalized == "depth_texture")
                return kResKey_DepthTexture;
            return FrameGraphResourceKey {.id = vbase::hashString(normalized)};
        }

        [[nodiscard]] std::unique_ptr<RenderFeature> makeBuiltinFeature(std::string_view id,
                                                                         IRenderService*  renderService)
        {
            const auto normalized = normalizeId(std::string(id));
            if (normalized == "compatibility_basecolor" || normalized == "basecolor" || normalized == "compatibility")
                return std::make_unique<CompatibilityBaseColorFeature>();
            if (normalized == "direct_gbuffer" || normalized == "gbuffer" || normalized == "deferred")
                return renderService ? std::make_unique<DirectGBufferFeature>(*renderService) : nullptr;
            if (normalized == "meshlet")
                return std::make_unique<MeshletFeature>();
            if (normalized == "mesh")
                return std::make_unique<MeshletFeature>();
            if (normalized == "general_gaussian_splat" || normalized == "gaussian_splat")
                return std::make_unique<GeneralGaussianSplatFeature>();
            if (normalized == "builtin_screen_space" || normalized == "screen_space" || normalized == "postprocess")
                return renderService ? std::make_unique<BuiltinScreenSpaceFeature>(*renderService) : nullptr;
            if (normalized == "final_composition" || normalized == "present")
                return std::make_unique<FinalCompositionFeature>();
            return {};
        }

        [[nodiscard]] sol::state makeAssetLuaState()
        {
            sol::state lua;
            lua.open_libraries(sol::lib::base, sol::lib::table, sol::lib::string, sol::lib::math);
            lua.set_function("RenderPipelineAsset", [](sol::table t) { return t; });
            lua.set_function("RenderFeature", [](sol::table t) { return t; });
            lua.set_function("ShaderLibrary", [](sol::table t) { return t; });
            return lua;
        }
    } // namespace

    class DeclarativeRenderer::FullscreenPassRuntime
    {
    public:
        explicit FullscreenPassRuntime(FullscreenPass desc, rhi::ShaderLibraryRuntime* shaderLibrary) :
            m_Desc(std::move(desc)), m_ShaderLibrary(shaderLibrary)
        {}

        void update(FullscreenPass desc, rhi::ShaderLibraryRuntime* shaderLibrary)
        {
            const bool pipelineKeyChanged = shaderLibrary != m_ShaderLibrary || desc.shader.library != m_Desc.shader.library ||
                                            desc.shader.vertex != m_Desc.shader.vertex ||
                                            desc.shader.fragment != m_Desc.shader.fragment;
            m_Desc = std::move(desc);
            m_ShaderLibrary = shaderLibrary;
            if (pipelineKeyChanged)
                m_Pipelines.clear();
        }

        FrameGraphResource addPass(FrameGraphBuildContext& ctx,
                                   FrameGraphResource      directInput = {},
                                   FrameGraphResource      directOutput = {},
                                   const bool              publishNamedOutput = true)
        {
            if (!ctx.view().target || !m_ShaderLibrary)
                return {};

            struct PassData
            {
                FrameGraphResource input;
                FrameGraphResource output;
            };

            const auto inputKey  = resourceKeyFor(m_Desc.input);
            const auto outputKey = resourceKeyFor(m_Desc.output);
            const auto input = directInput ? directInput : ctx.data.tryGet(inputKey);
            FrameGraphResource output =
                directOutput ? directOutput :
                normalizeId(m_Desc.output) == "backbuffer" || normalizeId(m_Desc.output) == "target" ?
                    framegraph::importTexture(ctx.fg, "DeclarativeBackbuffer", ctx.view().target) :
                    FrameGraphResource {};

            ctx.fg.addCallbackPass<PassData>(
                m_Desc.name.c_str(),
                [input, &output, &ctx, this](FrameGraph::Builder& builder, PassData& data) mutable {
                    if (input)
                    {
                        data.input = builder.read(input,
                                                  framegraph::TextureRead {
                                                      .binding =
                                                          {
                                                              .location      = {.set = 3, .binding = 0},
                                                              .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                                          },
                                                      .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                                      .imageAspect = rhi::ImageAspect::eColor,
                                                  });
                    }

                    if (!output)
                    {
                        output = builder.create<framegraph::FrameGraphTexture>(
                            m_Desc.name + " Color",
                            {
                                .extent     = ctx.view().extent,
                                .format     = rhi::PixelFormat::eRGBA8_UNorm,
                                .usageFlags = rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled,
                            });
                    }

                    data.output = builder.write(output,
                                                framegraph::Attachment {
                                                    .index       = 0,
                                                    .imageAspect = rhi::ImageAspect::eColor,
                                                    .clearValue  = input ? std::optional<framegraph::ClearValue> {} :
                                                                           std::optional<framegraph::ClearValue> {
                                                                               framegraph::ClearValue::eOpaqueBlack},
                                                });
                },
                [this](const PassData& data, FrameGraphPassResources&, void* ctxPtr) {
                    VULTRA_SCOPED_FRAMEGRAPH_EXEC_CONTEXT(rc, ctxPtr);
                    if (!m_ShaderLibrary)
                        return;

                    const auto framebufferInfo = rc.framebufferInfo();
                    if (!framebufferInfo)
                        return;

                    if (!data.input)
                    {
                        rc.cb.beginRendering(framebufferInfo.value()).endRendering();
                        return;
                    }

                    auto* pipeline = getPipeline(rc.rd, rhi::getColorFormat(framebufferInfo.value(), 0));
                    if (!pipeline)
                        return;

                    if (rc.resourceSet.contains(3) && rc.resourceSet[3].contains(0) && rc.ext.samplers.contains("linear"))
                        rc.overrideSampler(rc.resourceSet[3][0], rc.ext.samplers["linear"]);

                    rc.cb.bindPipeline(*pipeline);
                    rc.bindDescriptorSets(*pipeline);
                    if (m_Desc.pushConstants)
                    {
                        struct PushConstants
                        {
                            float exposure {1.0f};
                            int   method {0};
                        } pc {
                            .exposure = m_Desc.exposure,
                            .method   = m_Desc.method,
                        };
                        rc.cb.pushConstants(rhi::ShaderStages::eFragment, 0, &pc);
                    }
                    rc.cb.beginRendering(framebufferInfo.value()).drawFullScreenTriangle().endRendering();
                });

            if (publishNamedOutput && output && normalizeId(m_Desc.output) != "backbuffer" &&
                normalizeId(m_Desc.output) != "target")
                ctx.data.set(outputKey, output);

            return output;
        }

    private:
        rhi::GraphicsPipeline* getPipeline(rhi::RenderDevice& rd, const rhi::PixelFormat colorFormat)
        {
            const auto key = static_cast<uint32_t>(colorFormat);
            if (auto it = m_Pipelines.find(key); it != m_Pipelines.end())
                return &it->second;

            auto vertexShader = loadShader(m_Desc.shader.vertex, vshadersystem::ShaderStage::eVert);
            auto fragmentShader = loadShader(m_Desc.shader.fragment, vshadersystem::ShaderStage::eFrag);
            if (!vertexShader || !fragmentShader)
                return nullptr;

            auto builder = rhi::GraphicsPipeline::Builder {};
            builder.setColorFormats({colorFormat})
                .setInputAssembly({})
                .setDepthStencil({
                    .depthTest  = false,
                    .depthWrite = false,
                })
                .setRasterizer({
                    .polygonMode = rhi::PolygonMode::eFill,
                    .cullMode    = rhi::CullMode::eNone,
                })
                .setBlending(0, {.enabled = false});

            const bool webgpu = rd.getBackendApi() == rhi::RenderBackendApi::eWebGPU;
            if (webgpu)
            {
                builder
                    .addShader(rhi::ShaderType::eVertex,
                               {
                                   .code           = vertexShader->wgsl,
                                   .entryPointName = "main",
                                   .defines        = {},
                                   .reflection     = vertexShader->reflection,
                               })
                    .addShader(rhi::ShaderType::eFragment,
                               {
                                   .code           = fragmentShader->wgsl,
                                   .entryPointName = "main",
                                   .defines        = {},
                                   .reflection     = fragmentShader->reflection,
                               });
            }
            else
            {
                builder.addBuiltinShader(rhi::ShaderType::eVertex, *vertexShader)
                    .addBuiltinShader(rhi::ShaderType::eFragment, *fragmentShader);
            }

            auto pipeline = builder.build(rd);
            auto [it, inserted] = m_Pipelines.emplace(key, std::move(pipeline));
            static_cast<void>(inserted);
            return &it->second;
        }

        std::optional<rhi::ShaderLibraryRuntime::LoadedShader> loadShader(
            const std::string& shaderId,
            const vshadersystem::ShaderStage stage) const
        {
            if (!m_ShaderLibrary)
                return std::nullopt;

            const auto hash = rhi::ShaderLibraryRuntime::computeVariantHash(shaderId, stage, {});
            auto       shader = m_ShaderLibrary->load(hash, stage);
            if (!shader)
                VULTRA_CORE_ERROR("[DeclarativeRenderer] Failed to load shader '{}' for pass '{}'",
                                  shaderId,
                                  m_Desc.name);
            return shader;
        }

    private:
        FullscreenPass m_Desc;
        rhi::ShaderLibraryRuntime* m_ShaderLibrary {nullptr};
        std::unordered_map<uint32_t, rhi::GraphicsPipeline> m_Pipelines;
    };

    class DeclarativeRenderer::RenderGraphRuntime
    {
    public:
        RenderGraphRuntime(DeclarativeRenderer& owner, std::string uri, vrendergraph::RenderGraphDesc desc) :
            m_Owner(owner), m_Uri(std::move(uri)), m_Desc(std::move(desc))
        {
            registerPasses();
            registerResources();
        }

        void build(FrameGraphBuildContext& ctx)
        {
            vrendergraph::RenderGraphDesc activeDesc = m_Desc;
            activeDesc.passes.erase(std::remove_if(activeDesc.passes.begin(),
                                                   activeDesc.passes.end(),
                                                   [](const auto& pass) { return !pass.enabled; }),
                                    activeDesc.passes.end());
            if (activeDesc.passes.empty())
                return;

            vrendergraph::RenderGraph graph {
                m_Registry,
                [&ctx](FrameGraph& fg, const std::string_view resourceName) -> FrameGraphResource {
                    const auto normalized = normalizeId(std::string(resourceName));
                    if (normalized == "backbuffer" || normalized == "target")
                        return framegraph::importTexture(fg, "VRenderGraphBackbuffer", ctx.view().target);
                    return ctx.data.tryGet(resourceKeyFor(resourceName));
                }};

            m_Owner.m_CurrentBuildContext = &ctx;
            graph.build(ctx.fg, ctx.bb, activeDesc);
            m_Owner.m_CurrentBuildContext = nullptr;
        }

    private:
        void registerPasses()
        {
            m_Registry.registerPass(vrendergraph::PassDefinition {
                .type = "FullscreenShader",
                .setup =
                    [this](FrameGraph&,
                           FrameGraphBlackboard&,
                           const vrendergraph::ParamBlock& params,
                           vrendergraph::PassBuildContext& passCtx) {
                        auto* ctx = m_Owner.m_CurrentBuildContext;
                        if (!ctx)
                            return;

                        FullscreenPass pass;
                        pass.name = params.get<std::string>("name", "VRenderGraphFullscreen");
                        pass.input = "source";
                        pass.output = "color";
                        pass.shader.library = params.get<std::string>("library", "project");
                        pass.shader.vertex = params.get<std::string>("vertex", "fullscreen_triangle.vert");
                        pass.shader.fragment = params.get<std::string>("fragment", {});
                        pass.pushConstants = params.get<bool>("pushConstants", false);
                        pass.exposure = params.get<float>("exposure", 1.0f);
                        pass.method = params.get<int>("method", 0);

                        if (pass.shader.fragment.empty())
                        {
                            VULTRA_CORE_ERROR("[DeclarativeRenderer] FullscreenShader pass '{}' has no fragment shader",
                                              pass.name);
                            return;
                        }

                        auto* shaderService = m_Owner.getServices() ? m_Owner.getServices()->tryGet<IShaderService>() : nullptr;
                        if (!shaderService)
                            return;

                        rhi::ShaderLibraryRuntime* library = nullptr;
                        if (pass.shader.library == "builtin")
                            library = &shaderService->builtinLibrary();
                        else if (auto it = m_Owner.m_Asset.shaderLibraries.find(pass.shader.library);
                                 it != m_Owner.m_Asset.shaderLibraries.end())
                            library = shaderService->findProjectLibrary(it->second);

                        if (!library)
                        {
                            VULTRA_CORE_ERROR("[DeclarativeRenderer] Shader library '{}' is not loaded for graph pass '{}'",
                                              pass.shader.library,
                                              pass.name);
                            return;
                        }

                        auto& runtime = m_PassRuntimes[pass.name];
                        if (!runtime)
                            runtime = std::make_unique<FullscreenPassRuntime>(std::move(pass), library);
                        else
                            runtime->update(std::move(pass), library);
                        const auto input = passCtx.getInput("source");
                        const auto output = runtime->addPass(*ctx, input, {}, false);
                        if (output)
                        {
                            passCtx.setOutput("color", output);
                            const auto publish = params.get<std::string>("publish", {});
                            if (!publish.empty())
                                ctx->data.set(resourceKeyFor(publish), output);
                        }
                    },
                .inputs = {"source"},
                .outputs = {"color"},
                .params =
                    {
                        {.name = "name", .type = vrendergraph::ParamType::eString, .defaultValue = "VRenderGraphFullscreen"},
                        {.name = "library", .type = vrendergraph::ParamType::eString, .defaultValue = "project"},
                        {.name = "vertex", .type = vrendergraph::ParamType::eString, .defaultValue = "fullscreen_triangle.vert"},
                        {.name = "fragment", .type = vrendergraph::ParamType::eString, .defaultValue = ""},
                        {.name = "publish", .type = vrendergraph::ParamType::eString, .defaultValue = ""},
                        {.name = "pushConstants", .type = vrendergraph::ParamType::eBoolean, .defaultValue = false},
                        {.name = "exposure", .type = vrendergraph::ParamType::eFloat, .defaultValue = 1.0f, .minValue = 0.0f, .maxValue = 16.0f},
                        {.name = "method", .type = vrendergraph::ParamType::eInt, .defaultValue = 0, .minValue = 0, .maxValue = 2},
                    },
            });
        }

        void registerResources()
        {
            m_Registry.registerResource("final_composition_source");
            m_Registry.registerResource("color");
            m_Registry.registerResource("camera_color");
            m_Registry.registerResource("depth");
            m_Registry.registerResource("backbuffer");
            m_Registry.registerResource("target");
        }

    private:
        DeclarativeRenderer& m_Owner;
        std::string m_Uri;
        vrendergraph::RenderGraphDesc m_Desc;
        vrendergraph::RenderGraphRegistry m_Registry;
        std::unordered_map<std::string, std::unique_ptr<FullscreenPassRuntime>> m_PassRuntimes;
    };

    struct DeclarativeRenderer::RuntimeFeature
    {
        std::unique_ptr<RenderFeature>         builtin;
        std::unique_ptr<FullscreenPassRuntime> fullscreen;
        std::unique_ptr<RenderGraphRuntime>    renderGraph;

        void addPasses(FrameGraphBuildContext& ctx)
        {
            if (builtin)
                builtin->addPasses(ctx);
            if (fullscreen)
                fullscreen->addPass(ctx);
            if (renderGraph)
                renderGraph->build(ctx);
        }
    };

    DeclarativeRenderer::DeclarativeRenderer(std::string pipelineUri) : m_PipelineUri(std::move(pipelineUri)) {}

    DeclarativeRenderer::~DeclarativeRenderer() = default;

    void DeclarativeRenderer::init()
    {
        if (!loadPipelineAsset() || !loadShaderLibraries())
            return;

        m_RendererKey = m_Asset.rendererKey.empty() ? "custom" : m_Asset.rendererKey;
        buildRuntimeFeatures();
    }

    bool DeclarativeRenderer::buildRuntimeFeatures()
    {
        auto* services = getServices();
        auto* shaderService = services ? services->tryGet<IShaderService>() : nullptr;
        auto* assetService = services ? services->tryGet<IAssetService>() : nullptr;
        auto* renderService = services ? services->tryGet<IRenderService>() : nullptr;
        if (!shaderService)
            return false;

        for (const auto& feature : m_Asset.features)
        {
            if (!feature.builtin.empty())
            {
                auto builtin = makeBuiltinFeature(feature.builtin, renderService);
                if (!builtin)
                {
                    VULTRA_CORE_ERROR("[DeclarativeRenderer] Unknown builtin render feature '{}'", feature.builtin);
                    continue;
                }
                auto runtime = std::make_unique<RuntimeFeature>();
                runtime->builtin = std::move(builtin);
                m_RuntimeFeatures.push_back(std::move(runtime));
            }

            if (!feature.renderGraph.empty())
            {
                if (!assetService)
                    continue;

                auto text = assetService->loadTextAssetSync(feature.renderGraph);
                if (!text)
                {
                    VULTRA_CORE_ERROR("[DeclarativeRenderer] Failed to load render graph '{}': {}",
                                      feature.renderGraph,
                                      std::move(text).error());
                    continue;
                }

                try
                {
                    const auto json = nlohmann::json::parse(text.value());
                    auto       desc = vrendergraph::loadRenderGraph(json);
                    auto       runtime = std::make_unique<RuntimeFeature>();
                    runtime->renderGraph = std::make_unique<RenderGraphRuntime>(*this, feature.renderGraph, std::move(desc));
                    m_RuntimeFeatures.push_back(std::move(runtime));
                }
                catch (const std::exception& e)
                {
                    VULTRA_CORE_ERROR("[DeclarativeRenderer] Failed to parse render graph '{}': {}",
                                      feature.renderGraph,
                                      e.what());
                }
            }

            for (const auto& pass : feature.fullscreenPasses)
            {
                rhi::ShaderLibraryRuntime* library = nullptr;
                if (pass.shader.library == "builtin")
                    library = &shaderService->builtinLibrary();
                else if (auto it = m_Asset.shaderLibraries.find(pass.shader.library); it != m_Asset.shaderLibraries.end())
                    library = shaderService->findProjectLibrary(it->second);

                if (!library)
                {
                    VULTRA_CORE_ERROR("[DeclarativeRenderer] Shader library '{}' is not loaded for pass '{}'",
                                      pass.shader.library,
                                      pass.name);
                    continue;
                }

                auto runtime = std::make_unique<RuntimeFeature>();
                runtime->fullscreen = std::make_unique<FullscreenPassRuntime>(pass, library);
                m_RuntimeFeatures.push_back(std::move(runtime));
            }
        }

        return !m_RuntimeFeatures.empty();
    }

    void DeclarativeRenderer::buildFrameGraph(FrameGraphBuildContext& ctx)
    {
        for (auto& feature : m_RuntimeFeatures)
            feature->addPasses(ctx);
    }

    bool DeclarativeRenderer::loadPipelineAsset()
    {
        auto* services = getServices();
        auto* assetService = services ? services->tryGet<IAssetService>() : nullptr;
        if (!assetService)
            return false;

        auto text = assetService->loadTextAssetSync(m_PipelineUri);
        if (!text)
        {
            VULTRA_CORE_ERROR("[DeclarativeRenderer] Failed to load SRP asset '{}': {}", m_PipelineUri, std::move(text).error());
            return false;
        }

        auto lua = makeAssetLuaState();
        auto result = lua.safe_script(text.value(), &sol::script_pass_on_error);
        if (!result.valid())
        {
            sol::error err = result;
            VULTRA_CORE_ERROR("[DeclarativeRenderer] Lua error in SRP asset '{}': {}", m_PipelineUri, err.what());
            return false;
        }

        sol::object obj = result;
        if (!obj.is<sol::table>())
        {
            VULTRA_CORE_ERROR("[DeclarativeRenderer] SRP asset '{}' must return a table", m_PipelineUri);
            return false;
        }

        return parsePipelineTable(obj.as<sol::table>(), m_Asset);
    }

    bool DeclarativeRenderer::loadFeatureAsset(std::string_view uri, Feature& outFeature)
    {
        auto* services = getServices();
        auto* assetService = services ? services->tryGet<IAssetService>() : nullptr;
        if (!assetService)
            return false;

        auto text = assetService->loadTextAssetSync(uri);
        if (!text)
            return false;

        auto lua = makeAssetLuaState();
        auto result = lua.safe_script(text.value(), &sol::script_pass_on_error);
        if (!result.valid())
            return false;

        sol::object obj = result;
        return obj.is<sol::table>() && parseFeatureTable(obj.as<sol::table>(), outFeature);
    }

    bool DeclarativeRenderer::parsePipelineTable(sol::table table, PipelineAsset& outAsset)
    {
        outAsset.rendererKey = getString(table, "rendererKey", "custom");

        sol::object libsObj = table["shaderLibraries"];
        if (libsObj.is<sol::table>())
        {
            sol::table libs = libsObj.as<sol::table>();
            for (const auto& [key, value] : libs)
            {
                if (key.is<std::string>() && value.is<std::string>())
                    outAsset.shaderLibraries[key.as<std::string>()] = value.as<std::string>();
            }
        }

        sol::object featuresObj = table["features"];
        if (!featuresObj.is<sol::table>())
            return true;

        sol::table features = featuresObj.as<sol::table>();
        for (const auto& [_, value] : features)
        {
            static_cast<void>(_);
            Feature feature;
            if (value.is<std::string>())
            {
                const auto featureRef = value.as<std::string>();
                if (featureRef.starts_with("res://") || featureRef.find(".lua") != std::string::npos)
                {
                    if (featureRef.find(".vrg.json") != std::string::npos)
                    {
                        feature.name = featureRef;
                        feature.renderGraph = featureRef;
                        outAsset.features.push_back(std::move(feature));
                    }
                    else if (loadFeatureAsset(featureRef, feature))
                        outAsset.features.push_back(std::move(feature));
                }
                else
                {
                    feature.name = featureRef;
                    feature.builtin = featureRef;
                    outAsset.features.push_back(std::move(feature));
                }
            }
            else if (value.is<sol::table>() && parseFeatureTable(value.as<sol::table>(), feature))
            {
                outAsset.features.push_back(std::move(feature));
            }
        }
        return true;
    }

    bool DeclarativeRenderer::parseFeatureTable(sol::table table, Feature& outFeature)
    {
        outFeature.name = getString(table, "name", "LuaRenderFeature");
        outFeature.builtin = getString(table, "builtin");
        if (outFeature.builtin.empty())
            outFeature.builtin = getString(table, "useBuiltin");

        sol::object passesObj = table["passes"];
        if (!passesObj.is<sol::table>())
            return true;

        sol::table passes = passesObj.as<sol::table>();
        for (const auto& [_, passObj] : passes)
        {
            static_cast<void>(_);
            if (!passObj.is<sol::table>())
                continue;

            sol::table passTable = passObj.as<sol::table>();
            const auto type = getString(passTable, "type", "fullscreen");
            if (type != "fullscreen")
                continue;

            FullscreenPass pass;
            pass.name = getString(passTable, "name", outFeature.name + "::Fullscreen");
            sol::object shaderObj = passTable["shader"];
            if (!shaderObj.is<sol::table>())
                continue;

            sol::table shaderTable = shaderObj.as<sol::table>();
            pass.shader.library = getString(shaderTable, "library", "project");
            pass.shader.vertex = getString(shaderTable, "vertex");
            pass.shader.fragment = getString(shaderTable, "fragment");
            pass.input = getString(passTable, "input", pass.input);
            pass.output = getString(passTable, "output", pass.output);
            sol::object pushConstantsObj = passTable["pushConstants"];
            if (pushConstantsObj.is<bool>())
                pass.pushConstants = pushConstantsObj.as<bool>();
            sol::object exposureObj = passTable["exposure"];
            if (exposureObj.is<float>())
                pass.exposure = exposureObj.as<float>();
            else if (exposureObj.is<double>())
                pass.exposure = static_cast<float>(exposureObj.as<double>());
            sol::object methodObj = passTable["method"];
            if (methodObj.is<int>())
                pass.method = methodObj.as<int>();
            if (pass.shader.vertex.empty() || pass.shader.fragment.empty())
                continue;

            outFeature.fullscreenPasses.push_back(std::move(pass));
        }
        return true;
    }

    bool DeclarativeRenderer::loadShaderLibraries()
    {
        auto* services = getServices();
        auto* shaderService = services ? services->tryGet<IShaderService>() : nullptr;
        if (!shaderService)
            return false;

        bool ok = true;
        for (const auto& [name, uri] : m_Asset.shaderLibraries)
        {
            if (!shaderService->reloadProjectLibrary(uri))
            {
                VULTRA_CORE_ERROR("[DeclarativeRenderer] Failed to load shader library '{}' from '{}'", name, uri);
                ok = false;
            }
        }
        return ok;
    }
} // namespace vultra
