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
#include "vultra/function/rendering/srp/builtin/passes/build_indirect_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/coarse_instance_cull_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/compatibility_basecolor_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/deferred_lighting_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/depth_pre_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/direct_gbuffer_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/drawset_build_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/final_composition_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/fxaa_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/general_gaussian_splat_foveated_composite_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/general_gaussian_splat_preprocess_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/general_gaussian_splat_render_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/hzb_generate_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/meshlet_cull_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/meshlet_hiz_cull_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/raytracing_primary_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/selection_outline_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/shadow_map_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/skybox_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/ssao_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/ssr_composite_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/ssr_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/thin_gbuffer_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/tone_mapping_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/visibility_buffer_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/xr_view_synthesis_pass.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"
#include "vultra/function/rendering/srp/render_target_desc.hpp"
#include "vultra/function/services/asset_service.hpp"
#include "vultra/function/services/render_service.hpp"
#include "vultra/function/services/shader_service.hpp"

#include <fg/FrameGraph.hpp>
#include <nlohmann/json.hpp>
#include <vrendergraph/vrendergraph.hpp>

#include <vbase/core/hash.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

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

        [[nodiscard]] std::string rendererKeyFromRenderGraphUri(std::string_view uri)
        {
            auto filename = std::filesystem::path(std::string(uri)).filename().generic_string();
            constexpr std::array<std::string_view, 2> suffixes {".vrg.json", ".vrp.lua"};
            for (const auto suffix : suffixes)
            {
                if (filename.ends_with(suffix))
                {
                    filename.resize(filename.size() - suffix.size());
                    break;
                }
            }
            if (filename.empty())
                filename = "custom";
            return normalizeId(std::move(filename));
        }

        struct RenderGraphResRef
        {
            std::string node;
            std::string slot;
        };

        [[nodiscard]] std::optional<RenderGraphResRef> parseRenderGraphResRef(std::string_view ref)
        {
            if (ref.empty())
                return std::nullopt;

            const auto dot = ref.find('.');
            if (dot == std::string_view::npos)
                return RenderGraphResRef {std::string(ref), "out"};
            if (dot == 0 || dot + 1 >= ref.size())
                return std::nullopt;
            return RenderGraphResRef {std::string(ref.substr(0, dot)), std::string(ref.substr(dot + 1))};
        }

        [[nodiscard]] std::string makeRenderGraphResRef(std::string_view node, std::string_view slot)
        {
            if (slot == "out")
                return std::string(node);
            return std::string(node) + "." + std::string(slot);
        }

        [[nodiscard]] bool renderGraphConditionTokenMatches(std::string_view token, const RenderView& view)
        {
            auto normalized = normalizeId(std::string(token));
            while (!normalized.empty() && normalized.front() == '_')
                normalized.erase(normalized.begin());
            while (!normalized.empty() && normalized.back() == '_')
                normalized.pop_back();

            if (normalized.empty() || normalized == "always" || normalized == "true")
                return true;
            if (normalized == "never" || normalized == "false")
                return false;

            bool invert = false;
            if (normalized.front() == '!')
            {
                invert = true;
                normalized.erase(normalized.begin());
            }

            const bool xrView = view.usesSingleGraphStereo();
            bool       result = false;
            if (normalized == "xr" || normalized == "vr" || normalized == "stereo" ||
                normalized == "single_graph_stereo")
                result = xrView;
            else if (normalized == "non_xr" || normalized == "non_vr" || normalized == "mono")
                result = !xrView;

            return invert ? !result : result;
        }

        [[nodiscard]] bool renderGraphConditionMatches(std::string_view condition, const RenderView& view)
        {
            if (condition.empty())
                return true;

            const auto normalized = normalizeId(std::string(condition));
            size_t     orBegin    = 0;
            while (orBegin <= normalized.size())
            {
                const auto orEnd = normalized.find("||", orBegin);
                const auto group =
                    std::string_view(normalized)
                        .substr(orBegin, orEnd == std::string::npos ? std::string::npos : orEnd - orBegin);

                bool   groupMatches = true;
                size_t andBegin     = 0;
                while (andBegin <= group.size())
                {
                    const auto andEnd = group.find("&&", andBegin);
                    const auto token  = group.substr(
                        andBegin, andEnd == std::string_view::npos ? std::string_view::npos : andEnd - andBegin);
                    groupMatches = groupMatches && renderGraphConditionTokenMatches(token, view);
                    if (andEnd == std::string_view::npos)
                        break;
                    andBegin = andEnd + 2;
                }

                if (groupMatches)
                    return true;
                if (orEnd == std::string::npos)
                    break;
                orBegin = orEnd + 2;
            }
            return false;
        }

        struct RenderGraphPassPorts
        {
            std::vector<std::string> inputs;
            std::vector<std::string> outputs;
        };

        [[nodiscard]] RenderGraphPassPorts collectPassPorts(const vrendergraph::RenderGraphRegistry& registry,
                                                            const vrendergraph::PassDecl&            pass)
        {
            if (registry.contains(pass.type))
            {
                const auto& def = registry.get(pass.type);
                return {.inputs = def.inputs, .outputs = def.outputs};
            }

            RenderGraphPassPorts ports;
            ports.inputs.reserve(pass.inputs.size());
            for (const auto& [slot, _] : pass.inputs)
            {
                static_cast<void>(_);
                ports.inputs.push_back(slot);
            }

            ports.outputs.reserve(pass.outputs.size());
            for (const auto& [slot, _] : pass.outputs)
            {
                static_cast<void>(_);
                ports.outputs.push_back(slot);
            }
            return ports;
        }

        [[nodiscard]] FrameGraphResourceKey resourceKeyFor(std::string_view name)
        {
            const auto normalized = normalizeId(std::string(name));
            if (normalized == "final_composition_source" || normalized == "color" || normalized == "camera_color")
                return kResKey_FinalCompositionSource;
            if (normalized == "depth" || normalized == "depth_texture")
                return kResKey_DepthTexture;
            if (normalized == "gbuffer_color")
                return kResKey_GBufferColor;
            if (normalized == "gbuffer_normal" || normalized == "normal")
                return kResKey_GBufferNormal;
            if (normalized == "gbuffer_material" || normalized == "material")
                return kResKey_GBufferMetallicRoughnessAO;
            if (normalized == "gbuffer_entity_id" || normalized == "entity_id" || normalized == "entityid")
                return kResKey_GBufferEntityId;
            if (normalized == "ssao" || normalized == "ao")
                return kResKey_SsaoTexture;
            if (normalized == "ssr" || normalized == "reflection")
                return kResKey_SsrTexture;
            if (normalized == "visibility")
                return kResKey_VisibilityBuffer;
            if (normalized == "shadow_map" || normalized == "shadowmap")
                return kResKey_ShadowMap;
            if (normalized == "shadow_data" || normalized == "shadowdata")
                return kResKey_ShadowData;
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
            lua.set_function("RenderGraphPass", [](sol::table t) { return t; });
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
            const bool pipelineKeyChanged =
                shaderLibrary != m_ShaderLibrary || desc.shader.library != m_Desc.shader.library ||
                desc.shader.vertex != m_Desc.shader.vertex || desc.shader.fragment != m_Desc.shader.fragment;
            m_Desc          = std::move(desc);
            m_ShaderLibrary = shaderLibrary;
            if (pipelineKeyChanged)
                m_Pipelines.clear();
        }

        FrameGraphResource addPass(FrameGraphBuildContext& ctx,
                                   FrameGraphResource      directInput        = {},
                                   FrameGraphResource      directOutput       = {},
                                   const bool              publishNamedOutput = true)
        {
            if (!ctx.view().target || !m_ShaderLibrary)
                return {};

            struct PassData
            {
                FrameGraphResource input;
                FrameGraphResource output;
            };

            const auto         inputKey   = resourceKeyFor(m_Desc.input);
            const auto         outputKey  = resourceKeyFor(m_Desc.output);
            const auto         input      = directInput ? directInput : ctx.data.tryGet(inputKey);
            const auto         outputDesc = makeOutputDesc(ctx, input);
            FrameGraphResource output =
                directOutput ?
                    directOutput :
                normalizeId(m_Desc.output) == "backbuffer" || normalizeId(m_Desc.output) == "target" ?
                    framegraph::importTexture(
                        ctx.fg, "DeclarativeBackbuffer", ctx.view().target, ctx.view().renderTargetViewMask()) :
                    FrameGraphResource {};

            ctx.fg.addCallbackPass<PassData>(
                m_Desc.name.c_str(),
                [input, outputDesc, &output, &ctx, this](FrameGraph::Builder& builder, PassData& data) mutable {
                    if (input)
                    {
                        data.input =
                            builder.read(input,
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
                        output = builder.create<framegraph::FrameGraphTexture>(m_Desc.name + " Color", outputDesc);
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

                    auto* pipeline =
                        getPipeline(rc.rd, rhi::getColorFormat(framebufferInfo.value(), 0), framebufferInfo->viewMask);
                    if (!pipeline)
                        return;

                    if (rc.resourceSet.contains(3) && rc.resourceSet[3].contains(0) &&
                        rc.ext.samplers.contains("linear"))
                        rc.overrideSampler(rc.resourceSet[3][0], rc.ext.samplers["linear"]);

                    rc.cb.bindPipeline(*pipeline);
                    rc.bindDescriptorSets(*pipeline);
                    rc.cb.beginRendering(framebufferInfo.value()).drawFullScreenTriangle().endRendering();
                });

            if (publishNamedOutput && output && normalizeId(m_Desc.output) != "backbuffer" &&
                normalizeId(m_Desc.output) != "target")
                ctx.data.set(outputKey, output);

            return output;
        }

    private:
        framegraph::FrameGraphTexture::Desc makeOutputDesc(FrameGraphBuildContext&  ctx,
                                                           const FrameGraphResource input) const
        {
            auto desc = makeRenderViewTextureDesc(ctx.view(), rhi::PixelFormat::eRGBA16F);

            if (input)
            {
                const auto inputDesc = ctx.fg.getDescriptor<framegraph::FrameGraphTexture>(input);
                desc                 = makeInheritedTextureDesc(inputDesc, rhi::PixelFormat::eRGBA16F);
            }

            return desc;
        }

        rhi::GraphicsPipeline*
        getPipeline(rhi::RenderDevice& rd, const rhi::PixelFormat colorFormat, const uint32_t viewMask)
        {
            const uint64_t key = static_cast<uint64_t>(colorFormat) | (static_cast<uint64_t>(viewMask) << 32u);
            if (auto it = m_Pipelines.find(key); it != m_Pipelines.end())
                return &it->second;

            auto vertexShader   = loadShader(m_Desc.shader.vertex, vshadersystem::ShaderStage::eVert);
            auto fragmentShader = loadShader(m_Desc.shader.fragment, vshadersystem::ShaderStage::eFrag);
            if (!vertexShader || !fragmentShader)
                return nullptr;

            auto builder = rhi::GraphicsPipeline::Builder {};
            builder.setColorFormats({colorFormat})
                .setViewMask(viewMask)
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

            auto pipeline       = builder.build(rd);
            auto [it, inserted] = m_Pipelines.emplace(key, std::move(pipeline));
            static_cast<void>(inserted);
            return &it->second;
        }

        std::optional<rhi::ShaderLibraryRuntime::LoadedShader> loadShader(const std::string&               shaderId,
                                                                          const vshadersystem::ShaderStage stage) const
        {
            if (!m_ShaderLibrary)
                return std::nullopt;

            std::vector<std::string> shaderIds {shaderId};
            if (shaderId.find('/') == std::string::npos && shaderId.find('\\') == std::string::npos)
                shaderIds.push_back("fullscreen/" + shaderId);

            std::optional<rhi::ShaderLibraryRuntime::LoadedShader> shader;
            for (const auto& id : shaderIds)
            {
                const auto hash = rhi::ShaderLibraryRuntime::computeVariantHash(id, stage, {});
                if (!m_ShaderLibrary->hasVariant(hash, stage))
                    continue;
                shader = m_ShaderLibrary->load(hash, stage);
                if (shader)
                    break;
            }
            if (!shader)
                VULTRA_CORE_ERROR(
                    "[DeclarativeRenderer] Failed to load shader '{}' for pass '{}'", shaderId, m_Desc.name);
            return shader;
        }

    private:
        FullscreenPass                                      m_Desc;
        rhi::ShaderLibraryRuntime*                          m_ShaderLibrary {nullptr};
        std::unordered_map<uint64_t, rhi::GraphicsPipeline> m_Pipelines;
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
            vrendergraph::RenderGraphDesc activeDesc = makeActiveGraphWithPassthrough(m_Desc, ctx.view());
            if (activeDesc.passes.empty())
                return;

            std::string validationError;
            if (!validateActiveGraph(activeDesc, validationError))
            {
                if (validationError != m_LastValidationError)
                {
                    VULTRA_CORE_ERROR("[DeclarativeRenderer] Invalid render graph '{}': {}", m_Uri, validationError);
                    m_LastValidationError = validationError;
                }
                return;
            }
            m_LastValidationError.clear();

            vrendergraph::RenderGraph graph {
                m_Registry,
                [&ctx](
                    FrameGraph& fg, const std::string_view resourceName, const nlohmann::json&) -> FrameGraphResource {
                    const auto normalized = normalizeId(std::string(resourceName));
                    if (normalized == "backbuffer" || normalized == "target")
                        return framegraph::importTexture(
                            fg, "VRenderGraphBackbuffer", ctx.view().target, ctx.view().renderTargetViewMask());
                    return ctx.data.tryGet(resourceKeyFor(resourceName));
                }};

            m_Owner.m_CurrentBuildContext = &ctx;
            graph.build(ctx.fg, ctx.bb, activeDesc);
            m_Owner.m_CurrentBuildContext = nullptr;
        }

    private:
        vrendergraph::RenderGraphDesc makeActiveGraphWithPassthrough(const vrendergraph::RenderGraphDesc& desc,
                                                                     const RenderView&                    view) const
        {
            auto       activeDesc   = desc;
            const auto passIsActive = [&view](const vrendergraph::PassDecl& pass) {
                return pass.enabled && renderGraphConditionMatches(pass.when, view);
            };

            bool changed = true;
            while (changed)
            {
                changed = false;
                for (const auto& pass : activeDesc.passes)
                {
                    if (passIsActive(pass))
                        continue;

                    const auto ports = collectPassPorts(m_Registry, pass);
                    for (const auto& outputSlot : ports.outputs)
                    {
                        std::string replacement;
                        if (auto it = pass.inputs.find(outputSlot); it != pass.inputs.end() && !it->second.empty())
                            replacement = it->second;
                        else if (ports.inputs.size() == 1)
                        {
                            if (auto it = pass.inputs.find(ports.inputs.front());
                                it != pass.inputs.end() && !it->second.empty())
                                replacement = it->second;
                        }
                        if (replacement.empty())
                            continue;

                        const auto disabledOutput = makeRenderGraphResRef(pass.id, outputSlot);
                        for (auto& dst : activeDesc.passes)
                        {
                            for (auto& [_, ref] : dst.inputs)
                            {
                                static_cast<void>(_);
                                if (ref == disabledOutput)
                                {
                                    ref     = replacement;
                                    changed = true;
                                }
                            }
                        }
                    }
                }
            }

            activeDesc.passes.erase(std::remove_if(activeDesc.passes.begin(),
                                                   activeDesc.passes.end(),
                                                   [&passIsActive](const auto& pass) { return !passIsActive(pass); }),
                                    activeDesc.passes.end());
            return activeDesc;
        }

        bool validateActiveGraph(const vrendergraph::RenderGraphDesc& desc, std::string& error) const
        {
            std::unordered_set<std::string> resources;
            for (const auto& resource : desc.resources)
            {
                if (resource.name.empty())
                {
                    error = "external resource has empty name";
                    return false;
                }
                if (!resources.insert(resource.name).second)
                {
                    error = "duplicate external resource '" + resource.name + "'";
                    return false;
                }
            }

            std::unordered_map<std::string, const vrendergraph::PassDecl*> passes;
            for (const auto& pass : desc.passes)
            {
                if (pass.id.empty())
                {
                    error = "pass has empty id";
                    return false;
                }
                if (!passes.emplace(pass.id, &pass).second)
                {
                    error = "duplicate pass '" + pass.id + "'";
                    return false;
                }
                if (!m_Registry.contains(pass.type))
                {
                    error = "pass '" + pass.id + "' has unknown type '" + pass.type + "'";
                    return false;
                }
            }

            for (const auto& pass : desc.passes)
            {
                const auto&                           def = m_Registry.get(pass.type);
                const std::unordered_set<std::string> validInputs(def.inputs.begin(), def.inputs.end());
                const std::unordered_set<std::string> validOutputs(def.outputs.begin(), def.outputs.end());

                for (const auto& slot : def.inputs)
                {
                    const auto it = pass.inputs.find(slot);
                    if (it == pass.inputs.end() || it->second.empty())
                    {
                        error = "pass '" + pass.id + "' input '" + slot + "' is not connected";
                        return false;
                    }
                }

                for (const auto& [slot, ref] : pass.inputs)
                {
                    if (!validInputs.contains(slot))
                    {
                        error = "pass '" + pass.id + "' has unknown input slot '" + slot + "'";
                        return false;
                    }

                    const auto parsed = parseRenderGraphResRef(ref);
                    if (!parsed)
                    {
                        error = "pass '" + pass.id + "' input '" + slot + "' has invalid ref '" + ref + "'";
                        return false;
                    }

                    const auto srcPass = passes.find(parsed->node);
                    if (srcPass == passes.end())
                    {
                        if (!resources.contains(parsed->node))
                        {
                            error = "pass '" + pass.id + "' input '" + slot + "' references missing node '" +
                                    parsed->node + "'";
                            return false;
                        }
                        continue;
                    }

                    const auto& srcDef = m_Registry.get(srcPass->second->type);
                    if (std::find(srcDef.outputs.begin(), srcDef.outputs.end(), parsed->slot) == srcDef.outputs.end())
                    {
                        error = "pass '" + pass.id + "' input '" + slot + "' references missing output '" +
                                parsed->slot + "' on pass '" + parsed->node + "'";
                        return false;
                    }
                }

                for (const auto& [slot, _] : pass.outputs)
                {
                    static_cast<void>(_);
                    if (!validOutputs.contains(slot))
                    {
                        error = "pass '" + pass.id + "' has unknown output slot '" + slot + "'";
                        return false;
                    }
                }
            }

            return true;
        }

        void registerPasses()
        {
            for (const auto& projectPass : m_Owner.m_Asset.projectGraphPasses)
            {
                if (projectPass.type.empty() || projectPass.fullscreen.shader.fragment.empty())
                    continue;

                m_Registry.registerPass(vrendergraph::PassDefinition {
                    .type = projectPass.type,
                    .setup =
                        [this, projectPass](FrameGraph&,
                                            FrameGraphBlackboard&,
                                            const vrendergraph::ParamBlock& params,
                                            vrendergraph::PassBuildContext& passCtx) {
                            auto* ctx = m_Owner.m_CurrentBuildContext;
                            if (!ctx)
                                return;

                            auto* shaderService =
                                m_Owner.getServices() ? m_Owner.getServices()->tryGet<IShaderService>() : nullptr;
                            if (!shaderService)
                                return;

                            auto pass = projectPass.fullscreen;
                            pass.name =
                                params.get<std::string>("name", pass.name.empty() ? projectPass.type : pass.name);

                            rhi::ShaderLibraryRuntime* library = nullptr;
                            if (pass.shader.library == "builtin")
                                library = &shaderService->builtinLibrary();
                            else if (auto it = m_Owner.m_Asset.shaderLibraries.find(pass.shader.library);
                                     it != m_Owner.m_Asset.shaderLibraries.end())
                                library = shaderService->findProjectLibrary(it->second);

                            if (!library)
                            {
                                VULTRA_CORE_ERROR("[DeclarativeRenderer] Shader library '{}' is not loaded for project "
                                                  "graph pass '{}'",
                                                  pass.shader.library,
                                                  projectPass.type);
                                return;
                            }

                            auto& runtime = m_ProjectPassRuntimes[pass.name];
                            if (!runtime)
                                runtime = std::make_unique<FullscreenPassRuntime>(pass, library);
                            else
                                runtime->update(pass, library);

                            const auto input  = passCtx.getInput("source");
                            const auto output = runtime->addPass(*ctx, input, {}, false);
                            if (output)
                                passCtx.setOutput("color", output);
                        },
                    .inputs  = {"source"},
                    .outputs = {"color"},
                    .params =
                        {
                            {.name         = "name",
                             .type         = vrendergraph::ParamType::eString,
                             .defaultValue = projectPass.type},
                        },
                });
            }

            const auto registerBuiltin = [this](std::string               type,
                                                std::vector<std::string>  inputs,
                                                std::vector<std::string>  outputs,
                                                vrendergraph::PassSetupFn setup) {
                m_Registry.registerPass(vrendergraph::PassDefinition {
                    .type    = std::move(type),
                    .setup   = std::move(setup),
                    .inputs  = std::move(inputs),
                    .outputs = std::move(outputs),
                });
            };

            registerBuiltin("CompatibilityBaseColor",
                            {},
                            {"color"},
                            [this](FrameGraph&,
                                   FrameGraphBlackboard&,
                                   const vrendergraph::ParamBlock&,
                                   vrendergraph::PassBuildContext& passCtx) {
                                auto* ctx = m_Owner.m_CurrentBuildContext;
                                if (!ctx)
                                    return;
                                auto color = m_CompatibilityBaseColorPass.addPass(*ctx);
                                if (color)
                                {
                                    ctx->data.set(kResKey_FinalCompositionSource, color);
                                    passCtx.setOutput("color", color);
                                }
                            });

            registerBuiltin("DirectGBuffer",
                            {},
                            {"color", "depth", "normal", "material", "entityId"},
                            [this](FrameGraph&,
                                   FrameGraphBlackboard&,
                                   const vrendergraph::ParamBlock&,
                                   vrendergraph::PassBuildContext& passCtx) {
                                auto* ctx = m_Owner.m_CurrentBuildContext;
                                if (!ctx)
                                    return;
                                auto color = m_DirectGBufferPass.addPass(*ctx);
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
                                if (auto res = ctx->data.tryGet(kResKey_GBufferMetallicRoughnessAO))
                                    passCtx.setOutput("material", res);
                                if (auto res = ctx->data.tryGet(kResKey_GBufferEntityId))
                                    passCtx.setOutput("entityId", res);
                            });

            registerBuiltin("DepthPre",
                            {},
                            {"depth"},
                            [this](FrameGraph&,
                                   FrameGraphBlackboard&,
                                   const vrendergraph::ParamBlock&,
                                   vrendergraph::PassBuildContext& passCtx) {
                                auto* ctx = m_Owner.m_CurrentBuildContext;
                                if (!ctx)
                                    return;
                                m_DepthPrePass.addPass(*ctx);
                                if (auto depth = ctx->data.tryGet(kResKey_DepthTexture))
                                {
                                    passCtx.setOutput("depth", depth);
                                    if (ctx->view().stereoMode != StereoRenderMode::eMono)
                                        ctx->data.set(kResKey_StereoDepth, depth);
                                }
                            });

            registerBuiltin(
                "ShadowMap",
                {},
                {"shadowMap", "shadowData"},
                [this](FrameGraph&,
                       FrameGraphBlackboard&,
                       const vrendergraph::ParamBlock& params,
                       vrendergraph::PassBuildContext& passCtx) {
                    auto* ctx = m_Owner.m_CurrentBuildContext;
                    auto* renderService =
                        m_Owner.getServices() ? m_Owner.getServices()->tryGet<IRenderService>() : nullptr;
                    if (!ctx || !renderService)
                        return;
                    auto settings    = renderService->builtinRenderSettings().shadow;
                    settings.enabled = params.get<bool>("enabled", settings.enabled);
                    settings.resolution =
                        static_cast<uint32_t>(params.get<int>("resolution", static_cast<int>(settings.resolution)));
                    settings.cascadeCount =
                        static_cast<uint32_t>(params.get<int>("cascadeCount", static_cast<int>(settings.cascadeCount)));
                    settings.coverageRadius = params.get<float>("coverageRadius", settings.coverageRadius);
                    settings.lightDistance  = params.get<float>("lightDistance", settings.lightDistance);
                    settings.zRange         = params.get<float>("zRange", settings.zRange);
                    settings.splitLambda    = params.get<float>("splitLambda", settings.splitLambda);
                    settings.autoFitBounds  = params.get<bool>("autoFitBounds", settings.autoFitBounds);
                    settings.stableTexelSnapping =
                        params.get<bool>("stableTexelSnapping", settings.stableTexelSnapping);
                    settings.depthBias       = params.get<float>("depthBias", settings.depthBias);
                    settings.normalBias      = params.get<float>("normalBias", settings.normalBias);
                    settings.pcssLightRadius = params.get<float>("pcssLightRadius", settings.pcssLightRadius);
                    if (ctx->view().renderWorld)
                    {
                        for (const auto& light : ctx->view().renderWorld->lights)
                        {
                            if (light.kind == RenderLightKind::eDirectional && light.castsShadow)
                            {
                                settings.enabled        = settings.enabled;
                                settings.lightDirection = light.direction;
                                break;
                            }
                        }
                    }
                    auto shadow = m_ShadowMapPass.addPass(*ctx, settings);
                    if (shadow.shadowMap)
                        passCtx.setOutput("shadowMap", shadow.shadowMap);
                    if (shadow.shadowData)
                        passCtx.setOutput("shadowData", shadow.shadowData);
                });

            registerBuiltin(
                "DeferredLighting",
                {"color", "normal", "material", "depth", "ao", "shadowMap", "shadowData"},
                {"color"},
                [this](FrameGraph&,
                       FrameGraphBlackboard&,
                       const vrendergraph::ParamBlock& params,
                       vrendergraph::PassBuildContext& passCtx) {
                    auto* ctx = m_Owner.m_CurrentBuildContext;
                    auto* renderService =
                        m_Owner.getServices() ? m_Owner.getServices()->tryGet<IRenderService>() : nullptr;
                    if (!ctx || !renderService)
                        return;
                    const auto&   settings         = renderService->builtinRenderSettings();
                    auto          lightingSettings = settings.pbrLighting;
                    auto          shadowSettings   = settings.shadow;
                    rhi::Texture* skyboxTexture =
                        settings.pbrLighting.showSkybox ? settings.pbrLighting.environmentMap : nullptr;
                    lightingSettings.ambientIntensity =
                        params.get<float>("ambientIntensity", lightingSettings.ambientIntensity);
                    lightingSettings.shadowStrength =
                        params.get<float>("shadowStrength", lightingSettings.shadowStrength);
                    lightingSettings.iblIntensity  = params.get<float>("iblIntensity", lightingSettings.iblIntensity);
                    lightingSettings.debugViewMode = static_cast<PbrLightingSettings::DebugViewMode>(std::clamp(
                        params.get<int>("debugViewMode", static_cast<int>(lightingSettings.debugViewMode)), 0, 6));
                    shadowSettings.filterMode      = static_cast<ShadowRenderSettings::FilterMode>(std::clamp(
                        params.get<int>("shadowFilterMode", static_cast<int>(shadowSettings.filterMode)), 0, 2));
                    shadowSettings.debugMode       = static_cast<ShadowRenderSettings::DebugMode>(std::clamp(
                        params.get<int>("shadowDebugMode", static_cast<int>(shadowSettings.debugMode)), 0, 5));
                    if (params.get<bool>("debugCascades", false))
                        shadowSettings.debugMode = ShadowRenderSettings::DebugMode::eCascade;
                    const auto* renderEnvironment =
                        ctx->view().renderWorld && ctx->view().renderWorld->environment.active ?
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
                        skyboxTexture                     = renderEnvironment->skybox;
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
                        m_Owner.m_CurrentFrameApplyToneMapping = false;
                    shadowSettings.pcssBlockerSamples =
                        params.get<int>("pcssBlockerSamples", shadowSettings.pcssBlockerSamples);
                    shadowSettings.pcssFilterSamples = params.get<int>("pcfRadius", shadowSettings.pcssFilterSamples);
                    auto color                       = m_DeferredLightingPass.addPass(*ctx,
                                                                passCtx.getInput("color"),
                                                                passCtx.getInput("normal"),
                                                                passCtx.getInput("material"),
                                                                passCtx.getInput("depth"),
                                                                passCtx.getInput("ao"),
                                                                passCtx.getInput("shadowMap"),
                                                                passCtx.getInput("shadowData"),
                                                                shadowSettings,
                                                                lightingSettings,
                                                                ctx->view().renderWorld);
                    if (color)
                    {
                        const bool cameraWantsSkybox = (ctx->view().camera && ctx->view().camera->clearMode == 1u) ||
                                                       settings.pbrLighting.showSkybox;
                        if (cameraWantsSkybox && skyboxTexture && ctx->data.contains(kResKey_DepthTexture))
                        {
                            const auto env = framegraph::importTexture(ctx->fg, "Environment Map", skyboxTexture);
                            color          = m_SkyboxPass.addPass(*ctx,
                                                         color,
                                                         ctx->data.get(kResKey_DepthTexture),
                                                         env,
                                                         lightingSettings.environmentMap == skyboxTexture ?
                                                                      m_DeferredLightingPass.environmentCubemap() :
                                                                      nullptr);
                        }
                        ctx->data.set(kResKey_FinalCompositionSource, color);
                        if (ctx->view().stereoMode != StereoRenderMode::eMono)
                            ctx->data.set(kResKey_StereoColor, color);
                        passCtx.setOutput("color", color);
                    }
                });

            registerBuiltin("SsrComposite",
                            {"source", "reflection"},
                            {"color"},
                            [this](FrameGraph&,
                                   FrameGraphBlackboard&,
                                   const vrendergraph::ParamBlock& params,
                                   vrendergraph::PassBuildContext& passCtx) {
                                auto* ctx = m_Owner.m_CurrentBuildContext;
                                if (!ctx)
                                    return;
                                if (!params.get<bool>("enabled", true))
                                {
                                    passCtx.setOutput("color", passCtx.getInput("source"));
                                    return;
                                }
                                auto color = m_SsrCompositePass.addPass(
                                    *ctx, passCtx.getInput("source"), passCtx.getInput("reflection"));
                                if (color)
                                {
                                    ctx->data.set(kResKey_FinalCompositionSource, color);
                                    if (ctx->view().stereoMode != StereoRenderMode::eMono)
                                        ctx->data.set(kResKey_StereoColor, color);
                                    passCtx.setOutput("color", color);
                                }
                            });

            registerBuiltin("HzbGenerate",
                            {"depth"},
                            {"hzb"},
                            [this](FrameGraph&,
                                   FrameGraphBlackboard&,
                                   const vrendergraph::ParamBlock&,
                                   vrendergraph::PassBuildContext& passCtx) {
                                auto* ctx = m_Owner.m_CurrentBuildContext;
                                if (!ctx)
                                    return;
                                m_HzbGeneratePass.addPass(*ctx, passCtx.getInput("depth"));
                                if (auto hzb = ctx->data.tryGet(kResKey_HzbTexture))
                                    passCtx.setOutput("hzb", hzb);
                            });

            registerBuiltin("Ssao",
                            {"depth", "normal"},
                            {"ao"},
                            [this](FrameGraph&,
                                   FrameGraphBlackboard&,
                                   const vrendergraph::ParamBlock& params,
                                   vrendergraph::PassBuildContext& passCtx) {
                                auto* ctx = m_Owner.m_CurrentBuildContext;
                                auto* renderService =
                                    m_Owner.getServices() ? m_Owner.getServices()->tryGet<IRenderService>() : nullptr;
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
                                auto ao                  = m_SsaoPass.addPass(
                                    *ctx, passCtx.getInput("depth"), passCtx.getInput("normal"), settings);
                                if (ao)
                                {
                                    ctx->data.set(kResKey_SsaoTexture, ao);
                                    passCtx.setOutput("ao", ao);
                                }
                            });

            registerBuiltin("Ssr",
                            {"color", "depth", "normal", "material"},
                            {"reflection"},
                            [this](FrameGraph&,
                                   FrameGraphBlackboard&,
                                   const vrendergraph::ParamBlock& params,
                                   vrendergraph::PassBuildContext& passCtx) {
                                auto* ctx = m_Owner.m_CurrentBuildContext;
                                auto* renderService =
                                    m_Owner.getServices() ? m_Owner.getServices()->tryGet<IRenderService>() : nullptr;
                                if (!ctx || !renderService)
                                    return;
                                auto settings    = renderService->builtinRenderSettings().ssr;
                                settings.enabled = params.get<bool>("enabled", settings.enabled);
                                settings.reflectionFactor =
                                    params.get<float>("reflectionFactor", settings.reflectionFactor);
                                settings.maxSteps = params.get<int>("maxSteps", settings.maxSteps);
                                settings.binaryRefinement =
                                    params.get<int>("binaryRefinement", settings.binaryRefinement);
                                settings.stride    = params.get<float>("stride", settings.stride);
                                settings.thickness = params.get<float>("thickness", settings.thickness);
                                auto reflection    = m_SsrPass.addPass(*ctx,
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
                            });

            registerBuiltin("Fxaa",
                            {"source"},
                            {"color"},
                            [this](FrameGraph&,
                                   FrameGraphBlackboard&,
                                   const vrendergraph::ParamBlock& params,
                                   vrendergraph::PassBuildContext& passCtx) {
                                auto* ctx = m_Owner.m_CurrentBuildContext;
                                if (!ctx)
                                    return;
                                if (!params.get<bool>("enabled", true))
                                {
                                    passCtx.setOutput("color", passCtx.getInput("source"));
                                    return;
                                }
                                auto color = m_FxaaPass.addPass(*ctx, passCtx.getInput("source"));
                                if (color)
                                {
                                    ctx->data.set(kResKey_FinalCompositionSource, color);
                                    passCtx.setOutput("color", color);
                                }
                            });

            registerBuiltin("ToneMapping",
                            {"source"},
                            {"color"},
                            [this](FrameGraph&,
                                   FrameGraphBlackboard&,
                                   const vrendergraph::ParamBlock& params,
                                   vrendergraph::PassBuildContext& passCtx) {
                                auto* ctx = m_Owner.m_CurrentBuildContext;
                                if (!ctx)
                                    return;
                                const bool enabled =
                                    params.get<bool>("enabled", true) && m_Owner.m_CurrentFrameApplyToneMapping;
                                if (!enabled)
                                {
                                    passCtx.setOutput("color", passCtx.getInput("source"));
                                    return;
                                }
                                auto color = m_ToneMappingPass.addPass(*ctx,
                                                                       passCtx.getInput("source"),
                                                                       params.get<float>("exposure", 1.0f),
                                                                       params.get<int>("method", 0));
                                if (color)
                                {
                                    ctx->data.set(kResKey_FinalCompositionSource, color);
                                    passCtx.setOutput("color", color);
                                }
                            });

            registerBuiltin("SelectionOutline",
                            {"source", "entityId", "depth"},
                            {"color"},
                            [this](FrameGraph&,
                                   FrameGraphBlackboard&,
                                   const vrendergraph::ParamBlock& params,
                                   vrendergraph::PassBuildContext& passCtx) {
                                auto* ctx = m_Owner.m_CurrentBuildContext;
                                auto* renderService =
                                    m_Owner.getServices() ? m_Owner.getServices()->tryGet<IRenderService>() : nullptr;
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
                                auto color = m_SelectionOutlinePass.addPass(*ctx,
                                                                            passCtx.getInput("source"),
                                                                            passCtx.getInput("entityId"),
                                                                            passCtx.getInput("depth"),
                                                                            settings);
                                if (color)
                                {
                                    ctx->data.set(kResKey_FinalCompositionSource, color);
                                    passCtx.setOutput("color", color);
                                }
                            });

            registerBuiltin(
                "XrViewSynthesis",
                {"source", "depth"},
                {"color"},
                [this](FrameGraph&,
                       FrameGraphBlackboard&,
                       const vrendergraph::ParamBlock& params,
                       vrendergraph::PassBuildContext& passCtx) {
                    auto* ctx = m_Owner.m_CurrentBuildContext;
                    if (!ctx)
                        return;

                    XrViewSynthesisSettings settings {};
                    settings.warpingBackend = params.get<std::string>("warpingBackend", settings.warpingBackend);
                    settings.inpaintingBackend =
                        params.get<std::string>("inpaintingBackend", settings.inpaintingBackend);
                    settings.sourceView     = params.get<std::string>("sourceView", settings.sourceView);
                    settings.targetView     = params.get<std::string>("targetView", settings.targetView);
                    settings.baseGridSize   = static_cast<uint32_t>(std::max(1, params.get<int>("baseGridSize", 16)));
                    settings.maxSubdivision = static_cast<uint32_t>(std::max(0, params.get<int>("maxSubdivision", 2)));
                    settings.sideLengthThreshold =
                        std::max(0.0f, params.get<float>("sideLengthThreshold", settings.sideLengthThreshold));
                    settings.depthThreshold =
                        std::max(0.0f, params.get<float>("depthThreshold", settings.depthThreshold));

                    auto color = m_XrViewSynthesisPass.addPass(
                        *ctx, passCtx.getInput("source"), passCtx.getInput("depth"), settings);
                    if (color)
                    {
                        ctx->data.set(kResKey_FinalCompositionSource, color);
                        passCtx.setOutput("color", color);
                    }
                });

            registerBuiltin(
                "FinalComposition",
                {"source"},
                {"target"},
                [this](FrameGraph& fg,
                       FrameGraphBlackboard&,
                       const vrendergraph::ParamBlock&,
                       vrendergraph::PassBuildContext& passCtx) {
                    auto* ctx = m_Owner.m_CurrentBuildContext;
                    if (!ctx || !ctx->view().target)
                        return;
                    ctx->data.set(kResKey_FinalCompositionSource, passCtx.getInput("source"));
                    auto target = m_FinalCompositionPass.compose(
                        *ctx,
                        framegraph::importTexture(
                            fg, "VRenderGraphBackbuffer", ctx->view().target, ctx->view().renderTargetViewMask()));
                    if (target)
                        passCtx.setOutput("target", target);
                });

            registerBuiltin("RayTracingPrimary",
                            {},
                            {"color"},
                            [this](FrameGraph&,
                                   FrameGraphBlackboard&,
                                   const vrendergraph::ParamBlock&,
                                   vrendergraph::PassBuildContext& passCtx) {
                                auto* ctx = m_Owner.m_CurrentBuildContext;
                                if (!ctx)
                                    return;
                                auto color = m_RayTracingPrimaryPass.addPass(*ctx);
                                if (color)
                                {
                                    ctx->data.set(kResKey_FinalCompositionSource, color);
                                    passCtx.setOutput("color", color);
                                }
                            });

            registerBuiltin("VisibilityBuffer",
                            {},
                            {"visibility"},
                            [this](FrameGraph&,
                                   FrameGraphBlackboard&,
                                   const vrendergraph::ParamBlock&,
                                   vrendergraph::PassBuildContext& passCtx) {
                                auto* ctx = m_Owner.m_CurrentBuildContext;
                                if (!ctx)
                                    return;
                                auto visibility = m_VisibilityBufferPass.addPass(*ctx);
                                if (visibility)
                                    passCtx.setOutput("visibility", visibility);
                            });

            registerBuiltin("ThinGBuffer",
                            {"visibility"},
                            {"color", "normal", "material"},
                            [this](FrameGraph&,
                                   FrameGraphBlackboard&,
                                   const vrendergraph::ParamBlock&,
                                   vrendergraph::PassBuildContext& passCtx) {
                                auto* ctx = m_Owner.m_CurrentBuildContext;
                                if (!ctx)
                                    return;
                                auto color = m_ThinGBufferPass.addPass(*ctx, passCtx.getInput("visibility"));
                                if (color)
                                    passCtx.setOutput("color", color);
                                if (auto res = ctx->data.tryGet(kResKey_GBufferNormal))
                                    passCtx.setOutput("normal", res);
                                if (auto res = ctx->data.tryGet(kResKey_GBufferMetallicRoughnessAO))
                                    passCtx.setOutput("material", res);
                            });

            registerBuiltin("CoarseInstanceCull",
                            {},
                            {"visibleInstance", "visibleInstanceCount", "meshletCullDispatchArgs"},
                            [this](FrameGraph&,
                                   FrameGraphBlackboard&,
                                   const vrendergraph::ParamBlock&,
                                   vrendergraph::PassBuildContext& passCtx) {
                                auto* ctx = m_Owner.m_CurrentBuildContext;
                                if (!ctx)
                                    return;
                                m_CoarseInstanceCullPass.addPass(*ctx);
                                if (auto res = ctx->data.tryGet(kResKey_VisibleInstanceBuffer))
                                    passCtx.setOutput("visibleInstance", res);
                                if (auto res = ctx->data.tryGet(kResKey_VisibleInstanceCountBuffer))
                                    passCtx.setOutput("visibleInstanceCount", res);
                                if (auto res = ctx->data.tryGet(kResKey_MeshletCullDispatchArgsBuffer))
                                    passCtx.setOutput("meshletCullDispatchArgs", res);
                            });

            registerBuiltin("MeshletCull",
                            {},
                            {"visibleMeshlet", "visibleMeshletCount"},
                            [this](FrameGraph&,
                                   FrameGraphBlackboard&,
                                   const vrendergraph::ParamBlock&,
                                   vrendergraph::PassBuildContext& passCtx) {
                                auto* ctx = m_Owner.m_CurrentBuildContext;
                                if (!ctx)
                                    return;
                                m_MeshletCullPass.addPass(*ctx);
                                if (auto res = ctx->data.tryGet(kResKey_VisibleMeshletBuffer))
                                    passCtx.setOutput("visibleMeshlet", res);
                                if (auto res = ctx->data.tryGet(kResKey_VisibleMeshletCountBuffer))
                                    passCtx.setOutput("visibleMeshletCount", res);
                            });

            registerBuiltin("BuildIndirect",
                            {},
                            {"draw",
                             "instance",
                             "meshTable",
                             "transform",
                             "meshlets",
                             "visibleMeshlet",
                             "visibleMeshletCount",
                             "materialTable"},
                            [this](FrameGraph&,
                                   FrameGraphBlackboard&,
                                   const vrendergraph::ParamBlock&,
                                   vrendergraph::PassBuildContext& passCtx) {
                                auto* ctx = m_Owner.m_CurrentBuildContext;
                                if (!ctx)
                                    return;
                                m_BuildIndirectPass.addPass(*ctx);
                                if (auto res = ctx->data.tryGet(kResKey_DrawBuffer))
                                    passCtx.setOutput("draw", res);
                                if (auto res = ctx->data.tryGet(kResKey_InstanceBuffer))
                                    passCtx.setOutput("instance", res);
                                if (auto res = ctx->data.tryGet(kResKey_MeshTableBuffer))
                                    passCtx.setOutput("meshTable", res);
                                if (auto res = ctx->data.tryGet(kResKey_TransformBuffer))
                                    passCtx.setOutput("transform", res);
                                if (auto res = ctx->data.tryGet(kResKey_MeshletsBuffer))
                                    passCtx.setOutput("meshlets", res);
                                if (auto res = ctx->data.tryGet(kResKey_VisibleMeshletBuffer))
                                    passCtx.setOutput("visibleMeshlet", res);
                                if (auto res = ctx->data.tryGet(kResKey_VisibleMeshletCountBuffer))
                                    passCtx.setOutput("visibleMeshletCount", res);
                                if (auto res = ctx->data.tryGet(kResKey_MaterialTableBuffer))
                                    passCtx.setOutput("materialTable", res);
                            });

            registerBuiltin("DrawsetBuild",
                            {},
                            {"draw", "meshlets", "indirect", "drawSet"},
                            [this](FrameGraph&,
                                   FrameGraphBlackboard&,
                                   const vrendergraph::ParamBlock&,
                                   vrendergraph::PassBuildContext& passCtx) {
                                auto* ctx = m_Owner.m_CurrentBuildContext;
                                if (!ctx)
                                    return;
                                m_DrawsetBuildPass.addPass(*ctx);
                                if (auto res = ctx->data.tryGet(kResKey_DrawBuffer))
                                    passCtx.setOutput("draw", res);
                                if (auto res = ctx->data.tryGet(kResKey_MeshletsBuffer))
                                    passCtx.setOutput("meshlets", res);
                                if (auto res = ctx->data.tryGet(kResKey_IndirectBuffer))
                                    passCtx.setOutput("indirect", res);
                                if (auto res = ctx->data.tryGet(kResKey_DrawSetBuffer))
                                    passCtx.setOutput("drawSet", res);
                            });

            registerBuiltin("MeshletHiZCull",
                            {},
                            {"visibleMeshlet", "visibleMeshletCount"},
                            [this](FrameGraph&,
                                   FrameGraphBlackboard&,
                                   const vrendergraph::ParamBlock&,
                                   vrendergraph::PassBuildContext& passCtx) {
                                auto* ctx = m_Owner.m_CurrentBuildContext;
                                if (!ctx)
                                    return;
                                m_MeshletHiZCullPass.addPass(*ctx);
                                if (auto res = ctx->data.tryGet(kResKey_VisibleMeshletBuffer))
                                    passCtx.setOutput("visibleMeshlet", res);
                                if (auto res = ctx->data.tryGet(kResKey_VisibleMeshletCountBuffer))
                                    passCtx.setOutput("visibleMeshletCount", res);
                            });

            registerBuiltin("GeneralGaussianSplatPreprocess",
                            {},
                            {"draw",
                             "packedSource",
                             "selectedSource",
                             "visibleSplat",
                             "sortKey",
                             "sortIndex",
                             "visibleCount",
                             "indirect",
                             "sortStorage",
                             "sh"},
                            [this](FrameGraph&,
                                   FrameGraphBlackboard&,
                                   const vrendergraph::ParamBlock&,
                                   vrendergraph::PassBuildContext& passCtx) {
                                auto* ctx = m_Owner.m_CurrentBuildContext;
                                if (!ctx)
                                    return;
                                m_GaussianPreprocessPass.addPass(*ctx);
                                if (auto res = ctx->data.tryGet(kResKey_GeneralGaussianSplatDrawBuffer))
                                    passCtx.setOutput("draw", res);
                                if (auto res = ctx->data.tryGet(kResKey_GeneralGaussianSplatPackedSourceBuffer))
                                    passCtx.setOutput("packedSource", res);
                                if (auto res = ctx->data.tryGet(kResKey_GeneralGaussianSplatSelectedSourceBuffer))
                                    passCtx.setOutput("selectedSource", res);
                                if (auto res = ctx->data.tryGet(kResKey_GeneralGaussianSplatVisibleSplatBuffer))
                                    passCtx.setOutput("visibleSplat", res);
                                if (auto res = ctx->data.tryGet(kResKey_GeneralGaussianSplatSortKeyBuffer))
                                    passCtx.setOutput("sortKey", res);
                                if (auto res = ctx->data.tryGet(kResKey_GeneralGaussianSplatSortIndexBuffer))
                                    passCtx.setOutput("sortIndex", res);
                                if (auto res = ctx->data.tryGet(kResKey_GeneralGaussianSplatVisibleCountBuffer))
                                    passCtx.setOutput("visibleCount", res);
                                if (auto res = ctx->data.tryGet(kResKey_GeneralGaussianSplatIndirectBuffer))
                                    passCtx.setOutput("indirect", res);
                                if (auto res = ctx->data.tryGet(kResKey_GeneralGaussianSplatSortStorageBuffer))
                                    passCtx.setOutput("sortStorage", res);
                                if (auto res = ctx->data.tryGet(kResKey_GeneralGaussianSplatShBuffer))
                                    passCtx.setOutput("sh", res);
                            });

            registerBuiltin("GeneralGaussianSplatRender",
                            {},
                            {"color"},
                            [this](FrameGraph&,
                                   FrameGraphBlackboard&,
                                   const vrendergraph::ParamBlock&,
                                   vrendergraph::PassBuildContext& passCtx) {
                                auto* ctx = m_Owner.m_CurrentBuildContext;
                                if (!ctx)
                                    return;
                                auto color = m_GaussianRenderPass.addPass(*ctx);
                                if (color)
                                {
                                    ctx->data.set(kResKey_FinalCompositionSource, color);
                                    passCtx.setOutput("color", color);
                                }
                            });

            registerBuiltin("GeneralGaussianSplatFoveatedComposite",
                            {"fovea", "mid", "outer", "base"},
                            {"color"},
                            [this](FrameGraph&,
                                   FrameGraphBlackboard&,
                                   const vrendergraph::ParamBlock&,
                                   vrendergraph::PassBuildContext& passCtx) {
                                auto* ctx = m_Owner.m_CurrentBuildContext;
                                if (!ctx)
                                    return;
                                auto color = m_GaussianFoveatedCompositePass.compose(*ctx,
                                                                                     passCtx.getInput("fovea"),
                                                                                     passCtx.getInput("mid"),
                                                                                     passCtx.getInput("outer"),
                                                                                     passCtx.getInput("base"));
                                if (color)
                                {
                                    ctx->data.set(kResKey_FinalCompositionSource, color);
                                    passCtx.setOutput("color", color);
                                }
                            });
        }

        void registerResources()
        {
            for (const char* name : {
                     "final_composition_source",
                     "color",
                     "camera_color",
                     "depth",
                     "backbuffer",
                     "target",
                     "gbuffer_color",
                     "gbuffer_normal",
                     "gbuffer_material",
                     "gbuffer_entity_id",
                     "ssao",
                     "ao",
                     "ssr",
                     "reflection",
                     "visibility",
                     "shadow_map",
                     "shadow_data",
                     "stereo_color",
                     "stereo_depth",
                     "previous_stereo_color",
                     "previous_stereo_depth",
                     "previous_stereo_pose",
                     "stereo_reprojection_metadata",
                     "stereo_warped_color",
                     "stereo_inpainted_color",
                 })
                m_Registry.registerResource(name);
        }

    private:
        DeclarativeRenderer&                                                    m_Owner;
        std::string                                                             m_Uri;
        vrendergraph::RenderGraphDesc                                           m_Desc;
        vrendergraph::RenderGraphRegistry                                       m_Registry;
        std::unordered_map<std::string, std::unique_ptr<FullscreenPassRuntime>> m_ProjectPassRuntimes;
        std::string                                                             m_LastValidationError;
        CompatibilityBaseColorPass                                              m_CompatibilityBaseColorPass;
        DirectGBufferPass                                                       m_DirectGBufferPass;
        DepthPrePass                                                            m_DepthPrePass;
        ShadowMapPass                                                           m_ShadowMapPass;
        DeferredLightingPass                                                    m_DeferredLightingPass;
        SkyboxPass                                                              m_SkyboxPass;
        HzbGeneratePass                                                         m_HzbGeneratePass;
        SsaoPass                                                                m_SsaoPass;
        SsrPass                                                                 m_SsrPass;
        SsrCompositePass                                                        m_SsrCompositePass;
        FxaaPass                                                                m_FxaaPass;
        ToneMappingPass                                                         m_ToneMappingPass;
        SelectionOutlinePass                                                    m_SelectionOutlinePass;
        XrViewSynthesisPass                                                     m_XrViewSynthesisPass;
        FinalCompositionPass                                                    m_FinalCompositionPass;
        RayTracingPrimaryPass                                                   m_RayTracingPrimaryPass;
        VisibilityBufferPass                                                    m_VisibilityBufferPass;
        ThinGBufferPass                                                         m_ThinGBufferPass;
        CoarseInstanceCullPass                                                  m_CoarseInstanceCullPass;
        MeshletCullPass                                                         m_MeshletCullPass;
        BuildIndirectPass                                                       m_BuildIndirectPass;
        DrawsetBuildPass                                                        m_DrawsetBuildPass;
        MeshletHiZCullPass                                                      m_MeshletHiZCullPass;
        GeneralGaussianSplatPreprocessPass                                      m_GaussianPreprocessPass;
        GeneralGaussianSplatRenderPass                                          m_GaussianRenderPass;
        GeneralGaussianSplatFoveatedCompositePass                               m_GaussianFoveatedCompositePass;
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

    DeclarativeRenderer::DeclarativeRenderer(std::string pipelineUri, std::string rendererKey) :
        m_PipelineUri(std::move(pipelineUri)), m_RendererKeyOverride(std::move(rendererKey))
    {}

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
        auto* services      = getServices();
        auto* shaderService = services ? services->tryGet<IShaderService>() : nullptr;
        auto* assetService  = services ? services->tryGet<IAssetService>() : nullptr;
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
                auto runtime     = std::make_unique<RuntimeFeature>();
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
                    const auto json    = nlohmann::json::parse(text.value());
                    auto       desc    = vrendergraph::loadRenderGraph(json);
                    auto       runtime = std::make_unique<RuntimeFeature>();
                    runtime->renderGraph =
                        std::make_unique<RenderGraphRuntime>(*this, feature.renderGraph, std::move(desc));
                    m_RuntimeFeatures.push_back(std::move(runtime));
                }
                catch (const std::exception& e)
                {
                    VULTRA_CORE_ERROR(
                        "[DeclarativeRenderer] Failed to parse render graph '{}': {}", feature.renderGraph, e.what());
                }
            }

            for (const auto& pass : feature.fullscreenPasses)
            {
                rhi::ShaderLibraryRuntime* library = nullptr;
                if (pass.shader.library == "builtin")
                    library = &shaderService->builtinLibrary();
                else if (auto it = m_Asset.shaderLibraries.find(pass.shader.library);
                         it != m_Asset.shaderLibraries.end())
                    library = shaderService->findProjectLibrary(it->second);

                if (!library)
                {
                    VULTRA_CORE_ERROR("[DeclarativeRenderer] Shader library '{}' is not loaded for pass '{}'",
                                      pass.shader.library,
                                      pass.name);
                    continue;
                }

                auto runtime        = std::make_unique<RuntimeFeature>();
                runtime->fullscreen = std::make_unique<FullscreenPassRuntime>(pass, library);
                m_RuntimeFeatures.push_back(std::move(runtime));
            }
        }

        return !m_RuntimeFeatures.empty();
    }

    void DeclarativeRenderer::buildFrameGraph(FrameGraphBuildContext& ctx)
    {
        m_CurrentFrameApplyToneMapping = true;
        for (auto& feature : m_RuntimeFeatures)
            feature->addPasses(ctx);
    }

    bool DeclarativeRenderer::loadPipelineAsset()
    {
        auto* services     = getServices();
        auto* assetService = services ? services->tryGet<IAssetService>() : nullptr;
        if (!assetService)
            return false;

        auto text = assetService->loadTextAssetSync(m_PipelineUri);
        if (!text)
        {
            VULTRA_CORE_ERROR(
                "[DeclarativeRenderer] Failed to load SRP asset '{}': {}", m_PipelineUri, std::move(text).error());
            return false;
        }

        if (m_PipelineUri.ends_with(".vrg.json"))
        {
            try
            {
                const auto json = nlohmann::json::parse(text.value());
                static_cast<void>(vrendergraph::loadRenderGraph(json));
                auto feature        = Feature {};
                feature.name        = m_PipelineUri;
                feature.renderGraph = m_PipelineUri;
                m_Asset.rendererKey = m_RendererKeyOverride.empty() ? rendererKeyFromRenderGraphUri(m_PipelineUri) :
                                                                      m_RendererKeyOverride;
                m_Asset.shaderLibraries.try_emplace("project", "res://shaders/project.vshaderlib.lua");
                loadProjectGraphPasses();
                m_Asset.features.push_back(std::move(feature));
                return true;
            }
            catch (const std::exception& e)
            {
                VULTRA_CORE_ERROR(
                    "[DeclarativeRenderer] Failed to parse render graph pipeline '{}': {}", m_PipelineUri, e.what());
                return false;
            }
        }

        auto lua    = makeAssetLuaState();
        auto result = lua.safe_script(text.value(), &sol::script_pass_on_error);
        if (!result.valid())
        {
            sol::error err = result;
            VULTRA_CORE_ERROR(
                "[DeclarativeRenderer] Failed to parse render pipeline '{}': {}", m_PipelineUri, err.what());
            return false;
        }

        sol::object obj = result;
        if (!obj.is<sol::table>() || !parsePipelineTable(obj.as<sol::table>(), m_Asset))
        {
            VULTRA_CORE_ERROR("[DeclarativeRenderer] Render pipeline '{}' did not return a valid RenderPipelineAsset",
                              m_PipelineUri);
            return false;
        }

        if (!m_RendererKeyOverride.empty())
            m_Asset.rendererKey = m_RendererKeyOverride;
        m_Asset.shaderLibraries.try_emplace("project", "res://shaders/project.vshaderlib.lua");
        loadProjectGraphPasses();
        return true;
    }

    bool DeclarativeRenderer::loadFeatureAsset(std::string_view uri, Feature& outFeature)
    {
        auto* services     = getServices();
        auto* assetService = services ? services->tryGet<IAssetService>() : nullptr;
        if (!assetService)
            return false;

        auto text = assetService->loadTextAssetSync(uri);
        if (!text)
            return false;

        auto lua    = makeAssetLuaState();
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
                        feature.name        = featureRef;
                        feature.renderGraph = featureRef;
                        outAsset.features.push_back(std::move(feature));
                    }
                    else if (loadFeatureAsset(featureRef, feature))
                        outAsset.features.push_back(std::move(feature));
                }
                else
                {
                    feature.name    = featureRef;
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
        outFeature.name    = getString(table, "name", "LuaRenderFeature");
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
            const auto type      = getString(passTable, "type", "fullscreen");
            if (type != "fullscreen")
                continue;

            FullscreenPass pass;
            pass.name             = getString(passTable, "name", outFeature.name + "::Fullscreen");
            sol::object shaderObj = passTable["shader"];
            if (!shaderObj.is<sol::table>())
                continue;

            sol::table shaderTable = shaderObj.as<sol::table>();
            pass.shader.library    = getString(shaderTable, "library", "project");
            pass.shader.vertex     = getString(shaderTable, "vertex");
            pass.shader.fragment   = getString(shaderTable, "fragment");
            pass.input             = getString(passTable, "input", pass.input);
            pass.output            = getString(passTable, "output", pass.output);
            if (pass.shader.vertex.empty() || pass.shader.fragment.empty())
                continue;

            outFeature.fullscreenPasses.push_back(std::move(pass));
        }
        return true;
    }

    bool DeclarativeRenderer::parseProjectGraphPassTable(sol::table table, ProjectGraphPass& outPass)
    {
        outPass.type = getString(table, "type");
        if (outPass.type.empty())
            outPass.type = getString(table, "name");
        if (outPass.type.empty())
            return false;

        auto& pass  = outPass.fullscreen;
        pass.name   = getString(table, "passName", outPass.type);
        pass.input  = "source";
        pass.output = "color";

        sol::object shaderObj = table["shader"];
        if (!shaderObj.is<sol::table>())
            return false;

        sol::table shaderTable = shaderObj.as<sol::table>();
        pass.shader.library    = getString(shaderTable, "library", "project");
        pass.shader.vertex     = getString(shaderTable, "vertex", "fullscreen_triangle.vert");
        pass.shader.fragment   = getString(shaderTable, "fragment");
        return !pass.shader.vertex.empty() && !pass.shader.fragment.empty();
    }

    void DeclarativeRenderer::loadProjectGraphPasses()
    {
        auto* services     = getServices();
        auto* assetService = services ? services->tryGet<IAssetService>() : nullptr;
        if (!assetService)
            return;

        const auto passDir = std::filesystem::path(assetService->resolveUri("res://render/passes")).lexically_normal();
        std::error_code ec;
        if (!std::filesystem::is_directory(passDir, ec))
            return;

        std::vector<std::filesystem::path> files;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(passDir, ec))
        {
            if (ec)
                break;
            if (!entry.is_regular_file(ec) || entry.path().extension() != ".lua")
                continue;
            files.push_back(entry.path().lexically_normal());
        }
        std::sort(files.begin(), files.end());

        for (const auto& file : files)
        {
            std::ifstream stream(file);
            if (!stream.is_open())
                continue;

            std::stringstream buffer;
            buffer << stream.rdbuf();

            auto lua    = makeAssetLuaState();
            auto result = lua.safe_script(buffer.str(), &sol::script_pass_on_error);
            if (!result.valid())
            {
                sol::error err = result;
                VULTRA_CORE_ERROR("[DeclarativeRenderer] Failed to parse project graph pass '{}': {}",
                                  file.generic_string(),
                                  err.what());
                continue;
            }

            sol::object obj = result;
            if (!obj.is<sol::table>())
                continue;

            ProjectGraphPass pass;
            if (parseProjectGraphPassTable(obj.as<sol::table>(), pass))
                m_Asset.projectGraphPasses.push_back(std::move(pass));
        }
    }

    bool DeclarativeRenderer::loadShaderLibraries()
    {
        auto* services      = getServices();
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
