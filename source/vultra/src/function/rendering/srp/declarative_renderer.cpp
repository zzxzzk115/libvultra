#include "vultra/function/rendering/srp/declarative_renderer.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/draw_indirect_buffer.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/rhi/structs/framebuffer_info.hpp"
#include "vultra/core/rhi/structs/render_backend_api.hpp"
#include "vultra/core/rhi/texture.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"
#include "vultra/function/framegraph/framegraph_import.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"
#include "vultra/function/rendering/srp/builtin/builtin_pass_host.hpp"
#include "vultra/function/rendering/srp/builtin/builtin_render_graph_pass.hpp"
#include "vultra/function/rendering/srp/builtin/features/builtin_screen_space_feature.hpp"
#include "vultra/function/rendering/srp/builtin/features/compatibility_basecolor_feature.hpp"
#include "vultra/function/rendering/srp/builtin/features/direct_gbuffer_feature.hpp"
#include "vultra/function/rendering/srp/builtin/features/final_composition_feature.hpp"
#include "vultra/function/rendering/srp/builtin/features/general_gaussian_splat_feature.hpp"
#include "vultra/function/rendering/srp/builtin/features/meshlet_feature.hpp"
#include "vultra/function/rendering/srp/builtin/render_graph_backbuffer.hpp"

#include "declarative_lua_utils.hpp"
#include "vultra/function/rendering/srp/builtin/render_graph_resource_names.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"
#include "vultra/function/rendering/srp/render_target_desc.hpp"
#include "vultra/function/rendering/srp/upscaler_evaluate.hpp"
#include "vultra/function/services/asset_service.hpp"
#include "vultra/function/services/render_service.hpp"
#include "vultra/function/services/render_upscaler_service.hpp"
#include "vultra/function/services/shader_service.hpp"

#include <fg/FrameGraph.hpp>
#include <nlohmann/json.hpp>
#include <vrendergraph/vrendergraph.hpp>

#include <vbase/core/hash.hpp>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace vultra
{
    namespace
    {
        template<typename T>
        [[nodiscard]] T shaderParamDefaultValue(const vshadersystem::ParamDefault& value)
        {
            T out {};
            std::memcpy(&out, value.valueBuffer, std::min(sizeof(T), sizeof(value.valueBuffer)));
            return out;
        }

        template<typename T>
        void writePushConstantValue(std::vector<std::byte>&                 bytes,
                                    const vshadersystem::MaterialParamDesc& param,
                                    const T&                                value)
        {
            if (param.offset >= bytes.size())
                return;

            const auto size = std::min<size_t>({sizeof(T), param.size, bytes.size() - param.offset});
            std::memcpy(bytes.data() + param.offset, &value, size);
        }

        [[nodiscard]] std::vector<std::byte> packShaderParams(const vshadersystem::MaterialDescription& materialDesc,
                                                              const vrendergraph::ParamBlock&           params)
        {
            if (materialDesc.materialParamSize == 0u || materialDesc.params.empty())
                return {};

            std::vector<std::byte> bytes(materialDesc.materialParamSize);
            for (const auto& param : materialDesc.params)
            {
                switch (param.type)
                {
                    case vshadersystem::ParamType::eFloat: {
                        const auto fallback =
                            param.hasDefault ? shaderParamDefaultValue<float>(param.defaultValue) : 0.0f;
                        writePushConstantValue(bytes, param, params.get<float>(param.name, fallback));
                        break;
                    }
                    case vshadersystem::ParamType::eInt: {
                        const auto fallback =
                            param.hasDefault ? shaderParamDefaultValue<int32_t>(param.defaultValue) : int32_t {0};
                        writePushConstantValue(bytes, param, params.get<int>(param.name, fallback));
                        break;
                    }
                    case vshadersystem::ParamType::eUInt: {
                        const auto fallback =
                            param.hasDefault ? shaderParamDefaultValue<uint32_t>(param.defaultValue) : uint32_t {0};
                        const auto value =
                            static_cast<uint32_t>(std::max(params.get<int>(param.name, static_cast<int>(fallback)), 0));
                        writePushConstantValue(bytes, param, value);
                        break;
                    }
                    case vshadersystem::ParamType::eBool: {
                        const int32_t fallback =
                            param.hasDefault && shaderParamDefaultValue<bool>(param.defaultValue) ? 1 : 0;
                        const int32_t value = params.get<bool>(param.name, fallback != 0) ? 1 : 0;
                        writePushConstantValue(bytes, param, value);
                        break;
                    }
                    default:
                        break;
                }
            }
            return bytes;
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

        void materializeRenderGraphDefaultOutputs(const vrendergraph::RenderGraphRegistry& registry,
                                                  vrendergraph::RenderGraphDesc&           desc)
        {
            for (auto& pass : desc.passes)
            {
                if (!registry.contains(pass.type))
                    continue;

                const auto& def = registry.get(pass.type);
                for (const auto& slot : def.outputs)
                    pass.outputs.try_emplace(slot, makeRenderGraphResRef(pass.id, slot));
            }
        }

        bool applyRenderGraphTopoOrder(vrendergraph::RenderGraphDesc& desc, std::string* error)
        {
            std::unordered_map<std::string, size_t> order;
            for (size_t i = 0; i < desc.passes.size(); ++i)
                order[desc.passes[i].id] = i;

            std::unordered_map<std::string, std::vector<std::string>> edges;
            std::unordered_map<std::string, int>                      indegree;
            for (const auto& pass : desc.passes)
                indegree.try_emplace(pass.id, 0);

            for (const auto& pass : desc.passes)
            {
                for (const auto& [_, ref] : pass.inputs)
                {
                    static_cast<void>(_);
                    const auto parsed = parseRenderGraphResRef(ref.resource);
                    if (!parsed || !order.contains(parsed->node))
                        continue;
                    edges[parsed->node].push_back(pass.id);
                    ++indegree[pass.id];
                }
            }

            std::vector<std::string> ready;
            for (const auto& [id, degree] : indegree)
            {
                if (degree == 0)
                    ready.push_back(id);
            }

            std::vector<std::string> sorted;
            while (!ready.empty())
            {
                std::sort(
                    ready.begin(), ready.end(), [&](const auto& a, const auto& b) { return order[a] < order[b]; });
                const auto id = ready.front();
                ready.erase(ready.begin());
                sorted.push_back(id);

                for (const auto& dst : edges[id])
                {
                    if (--indegree[dst] == 0)
                        ready.push_back(dst);
                }
            }

            if (sorted.size() != desc.passes.size())
            {
                if (error)
                    *error = "render graph contains a pass dependency cycle";
                return false;
            }

            std::vector<vrendergraph::PassDecl> reordered;
            reordered.reserve(desc.passes.size());
            for (const auto& id : sorted)
            {
                auto it = std::find_if(
                    desc.passes.begin(), desc.passes.end(), [&](const auto& pass) { return pass.id == id; });
                if (it != desc.passes.end())
                    reordered.push_back(std::move(*it));
            }
            desc.passes = std::move(reordered);
            return true;
        }

        [[nodiscard]] bool isDepthFormat(rhi::PixelFormat format)
        {
            switch (format)
            {
                case rhi::PixelFormat::eDepth16:
                case rhi::PixelFormat::eDepth32F:
                case rhi::PixelFormat::eDepth16_Stencil8:
                case rhi::PixelFormat::eDepth24_Stencil8:
                case rhi::PixelFormat::eDepth32F_Stencil8:
                    return true;
                default:
                    return false;
            }
        }

        // A graph is "explicit-per-eye" when it names the two XR eyes itself rather than
        // relying on single-graph multiview: it declares a left/right role resource, or any
        // pass references a left/right eye target (by resource name or `view` selector). Such
        // a graph is rendered ONCE (mono source) and routes each eye via its own
        // FinalComposition target; render_system must NOT force multiview for it.
        [[nodiscard]] bool refIsExplicitEye(const vrendergraph::ResourceRef& ref)
        {
            return backbufferViewFromResource(ref.resource) != RenderGraphBackbufferView::eCurrent ||
                   backbufferViewFromSelector(ref.selector, RenderGraphBackbufferView::eCurrent) !=
                       RenderGraphBackbufferView::eCurrent;
        }

        [[nodiscard]] bool graphPrefersExplicitPerEye(const vrendergraph::RenderGraphDesc& desc)
        {
            for (const auto& pass : desc.passes)
            {
                for (const auto& [_, ref] : pass.inputs)
                    if (refIsExplicitEye(ref))
                        return true;
                for (const auto& [_, ref] : pass.outputs)
                    if (refIsExplicitEye(ref))
                        return true;
            }
            return false;
        }

        void warnFullscreenMultiviewContractOnce(std::string_view passName)
        {
            static std::unordered_set<std::string> warned;
            const auto                             key = std::string(passName);
            if (!warned.insert(key).second)
                return;
            VULTRA_CORE_WARN("[DeclarativeRenderer] Fullscreen pass '{}' reads multiview input but its output does not "
                             "preserve viewMask.",
                             passName);
        }

        // Device/platform capabilities a render-graph `when` condition can branch on. Sourced from the
        // render device at build time (see DeclarativeRenderer::RenderGraphRuntime::build). Lets a single
        // graph degrade gracefully across backends/platforms/feature tiers instead of swapping whole graphs.
        struct RenderGraphCapabilities
        {
            rhi::RenderBackendApi              backend {rhi::RenderBackendApi::eVulkan};
            rhi::RenderDeviceFeatureFlagBits   features {rhi::RenderDeviceFeatureFlagBits::eNormal};
            rhi::RenderDeviceFeatureReportFlagBits featureReport {rhi::RenderDeviceFeatureReportFlagBits::eNone};
            bool                               tierHighend {true};
            bool                               platformAndroid {false};
            bool                               platformWeb {false};
        };

        [[nodiscard]] bool renderGraphConditionTokenMatches(std::string_view                token,
                                                            const RenderView&               view,
                                                            const RenderGraphCapabilities& caps)
        {
            auto normalized = normalizeRenderGraphId(std::string(token));
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

            const bool xrView          = view.usesSingleGraphStereo();
            const bool hasXrEyeTargets = view.xrEyeTargets[0] != nullptr && view.xrEyeTargets[1] != nullptr;
            bool       result          = false;
            if (normalized == "xr" || normalized == "vr" || normalized == "stereo" ||
                normalized == "single_graph_stereo")
                result = xrView;
            else if (normalized == "xr_eye_targets" || normalized == "eye_targets" ||
                     normalized == "explicit_eye_targets")
                result = hasXrEyeTargets;
            else if (normalized == "xr_preview" || normalized == "stereo_preview")
                result = xrView && !hasXrEyeTargets;
            else if (normalized == "non_xr" || normalized == "non_vr" || normalized == "mono")
                result = !xrView;
            // Backend / platform / feature-tier capability predicates.
            else if (normalized == "backend_vulkan")
                result = caps.backend == rhi::RenderBackendApi::eVulkan;
            else if (normalized == "backend_webgpu")
                result = caps.backend == rhi::RenderBackendApi::eWebGPU;
            else if (normalized == "tier_highend")
                result = caps.tierHighend;
            else if (normalized == "tier_compat" || normalized == "tier_compatibility")
                result = !caps.tierHighend;
            else if (normalized == "platform_web")
                result = caps.platformWeb;
            else if (normalized == "platform_android")
                result = caps.platformAndroid;
            else if (normalized == "platform_desktop")
                result = !caps.platformWeb && !caps.platformAndroid;
            else if (normalized == "feature_raytracing")
                result = HasFlagValues(caps.features, rhi::RenderDeviceFeatureFlagBits::eRayTracing);
            else if (normalized == "feature_rayquery")
                result = HasFlagValues(caps.features, rhi::RenderDeviceFeatureFlagBits::eRayQuery);
            else if (normalized == "feature_raytracing_pipeline")
                result = HasFlagValues(caps.features, rhi::RenderDeviceFeatureFlagBits::eRayTracingPipeline);
            else if (normalized == "feature_meshshader")
                result = HasFlagValues(caps.features, rhi::RenderDeviceFeatureFlagBits::eMeshShader);
            else if (normalized == "feature_xr")
                result = HasFlagValues(caps.features, rhi::RenderDeviceFeatureFlagBits::eXR);
            // Bindless / descriptor-indexing: the true boundary for the deferred GBuffer path (it
            // indexes a nonuniform bindless texture array). Vulkan reports it via descriptor indexing;
            // core WebGPU has no portable bindless, so this is false there -> compat forward path.
            else if (normalized == "feature_bindless" || normalized == "feature_descriptor_indexing")
                result = HasFlagValues(caps.featureReport, rhi::RenderDeviceFeatureReportFlagBits::eDescriptorIndexing);

            return invert ? !result : result;
        }

        [[nodiscard]] bool renderGraphViewModeMatches(std::string_view viewMode, const RenderView& view)
        {
            const auto normalized = normalizeRenderGraphId(std::string(viewMode));
            if (normalized.empty() || normalized == "inherit" || normalized == "any" || normalized == "always")
                return true;
            if (normalized == "xr" || normalized == "vr" || normalized == "stereo" ||
                normalized == "single_graph_stereo")
                return view.usesSingleGraphStereo();
            if (normalized == "mono" || normalized == "non_xr" || normalized == "non_vr")
                return !view.usesSingleGraphStereo();
            return true;
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
            const auto normalized = normalizeRenderGraphId(std::string(name));
            if (normalized == "final_composition_source" || normalized == "color" || normalized == "camera_color")
                return kResKey_FinalCompositionSource;
            if (normalized == "depth" || normalized == "depth_texture")
                return kResKey_DepthTexture;
            if (normalized == "gbuffer_color")
                return kResKey_GBufferColor;
            if (normalized == "gbuffer_normal" || normalized == "normal")
                return kResKey_GBufferNormal;
            if (normalized == "gbuffer_material" || normalized == "material")
                return kResKey_GBufferMaterial;
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
            if (normalized == "skin_matrix" || normalized == "skinmatrix" || normalized == "skin_matrices" ||
                normalized == "skinmatrices")
                return kResKey_SkinMatrixBuffer;
            return FrameGraphResourceKey {.id = vbase::hashString(normalized)};
        }

        [[nodiscard]] std::unique_ptr<RenderFeature> makeBuiltinFeature(std::string_view id,
                                                                        IRenderService*  renderService)
        {
            const auto normalized = normalizeRenderGraphId(std::string(id));
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

        void importDeclarativeGpuSceneBuffers(FrameGraphBuildContext& ctx)
        {
            auto* gpuSceneDatabase = ctx.view().gpuSceneDatabase;
            if (!gpuSceneDatabase || !gpuSceneDatabase->resources)
                return;

            const auto importStorage = [&ctx](const FrameGraphResourceKey key, const char* name, rhi::Buffer* buffer) {
                if (!buffer || !(*buffer) || ctx.data.contains(key))
                    return;
                ctx.data.set(key,
                             framegraph::importBuffer(ctx.fg, name, buffer, framegraph::BufferType::eStorageBuffer));
            };
            importStorage(kResKey_InstanceBuffer, "ImportedInstanceBuffer", gpuSceneDatabase->instanceBuffer.get());
            importStorage(kResKey_MeshTableBuffer, "ImportedMeshTableBuffer", gpuSceneDatabase->meshTableBuffer.get());
            importStorage(kResKey_TransformBuffer, "ImportedTransformBuffer", gpuSceneDatabase->transformBuffer.get());
            importStorage(
                kResKey_SkinMatrixBuffer, "ImportedSkinMatrixBuffer", gpuSceneDatabase->skinMatrixBuffer.get());
            importStorage(kResKey_MeshletsBuffer,
                          "ImportedMeshletsBuffer",
                          gpuSceneDatabase->resources->meshlets.meshletsBuffer.get());
            importStorage(kResKey_MaterialTableBuffer,
                          "ImportedMaterialTableBuffer",
                          gpuSceneDatabase->resources->materialTableBuffer.get());
            importStorage(kResKey_MaterialParametersBuffer,
                          "ImportedMaterialParamsBuffer",
                          gpuSceneDatabase->resources->materialParams.gpu.get());
            importStorage(kResKey_MeshletVertexBuffer,
                          "ImportedMeshletVertexBuffer",
                          gpuSceneDatabase->resources->meshlets.meshletVerticesBuffer.get());
            importStorage(kResKey_MeshletTriangleBuffer,
                          "ImportedMeshletTriangleBuffer",
                          gpuSceneDatabase->resources->meshlets.meshletTrianglesBuffer.get());

            auto* gpuSceneView = ctx.view().gpuSceneView;
            if (gpuSceneView)
            {
                importStorage(kResKey_DrawBuffer, "ImportedDrawBuffer", gpuSceneView->drawBuffer.get());
                if (gpuSceneView->indirectBuffer)
                {
                    ctx.data.set(kResKey_IndirectBuffer,
                                 framegraph::importBuffer(ctx.fg,
                                                          "ImportedIndirectBuffer",
                                                          &(*gpuSceneView->indirectBuffer),
                                                          framegraph::BufferType::eDrawIndirectBuffer,
                                                          gpuSceneView->indirectBuffer->getStride()));
                }
            }
        }
    } // namespace
    // BuiltinPassHost: live, friend-access bridge from a builtin pass's build() body to
    // the owning renderer's per-frame state (see builtin_pass_host.hpp).
    FrameGraphBuildContext* BuiltinPassHost::currentBuildContext() const { return m_Owner.m_CurrentBuildContext; }

    vbase::ServiceRegistry* BuiltinPassHost::services() const { return m_Owner.getServices(); }

    IRenderService* BuiltinPassHost::renderService() const
    {
        auto* svc = m_Owner.getServices();
        return svc ? svc->tryGet<IRenderService>() : nullptr;
    }

    bool BuiltinPassHost::applyToneMappingThisFrame() const { return m_Owner.m_CurrentFrameApplyToneMapping; }

    void BuiltinPassHost::setApplyToneMappingThisFrame(bool value) { m_Owner.m_CurrentFrameApplyToneMapping = value; }

    class DeclarativeRenderer::FullscreenPassRuntime
    {
    public:
        explicit FullscreenPassRuntime(FullscreenPass             desc,
                                       rhi::ShaderLibraryRuntime* vertexShaderLibrary,
                                       rhi::ShaderLibraryRuntime* fragmentShaderLibrary) :
            m_Desc(std::move(desc)), m_VertexShaderLibrary(vertexShaderLibrary),
            m_FragmentShaderLibrary(fragmentShaderLibrary)
        {}

        void update(FullscreenPass             desc,
                    rhi::ShaderLibraryRuntime* vertexShaderLibrary,
                    rhi::ShaderLibraryRuntime* fragmentShaderLibrary)
        {
            const bool pipelineKeyChanged =
                vertexShaderLibrary != m_VertexShaderLibrary || fragmentShaderLibrary != m_FragmentShaderLibrary ||
                desc.shader.library != m_Desc.shader.library ||
                desc.shader.vertexLibrary != m_Desc.shader.vertexLibrary ||
                desc.shader.fragmentLibrary != m_Desc.shader.fragmentLibrary ||
                desc.shader.vertex != m_Desc.shader.vertex || desc.shader.fragment != m_Desc.shader.fragment;
            m_Desc                  = std::move(desc);
            m_VertexShaderLibrary   = vertexShaderLibrary;
            m_FragmentShaderLibrary = fragmentShaderLibrary;
            if (pipelineKeyChanged)
                m_Pipelines.clear();
        }

        void invalidatePipelines() { m_Pipelines.clear(); }

        FrameGraphResource addPass(FrameGraphBuildContext&         ctx,
                                   FrameGraphResource              directInput        = {},
                                   FrameGraphResource              directOutput       = {},
                                   const bool                      publishNamedOutput = true,
                                   const vrendergraph::ParamBlock& params             = {},
                                   std::vector<FrameGraphResource> extraInputs        = {})
        {
            if (!ctx.view().target || !m_VertexShaderLibrary || !m_FragmentShaderLibrary)
                return {};

            struct PassData
            {
                FrameGraphResource input;
                FrameGraphResource output;
            };

            const auto         inputKey      = resourceKeyFor(m_Desc.input);
            const auto         outputKey     = resourceKeyFor(m_Desc.output);
            const auto         input         = directInput ? directInput : ctx.data.tryGet(inputKey);
            const auto         outputDesc    = makeOutputDesc(ctx, input);
            const auto         pushConstants = makePushConstants(params);
            FrameGraphResource output {};
            if (directOutput)
                output = directOutput;
            else if (isBackbufferResource(m_Desc.output))
            {
                output = importRenderGraphBackbuffer(
                    ctx.fg, ctx.view(), m_Desc.output, nlohmann::json::object(), "DeclarativeBackbuffer");
            }

            if (input)
            {
                const auto inputDesc = ctx.fg.getDescriptor<framegraph::FrameGraphTexture>(input);
                if (inputDesc.viewMask != 0u && outputDesc.viewMask == 0u && !directOutput &&
                    !isBackbufferResource(m_Desc.output))
                {
                    warnFullscreenMultiviewContractOnce(m_Desc.name);
                }
            }

            ctx.fg.addCallbackPass<PassData>(
                m_Desc.name.c_str(),
                [input, outputDesc, &output, &ctx, extraInputs = std::move(extraInputs), this](
                    FrameGraph::Builder& builder, PassData& data) mutable {
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

                    // Additional declared inputs bind to set=3, binding=1,2,... so a project
                    // (Lua) fragment shader can sample engine resources (depth, gbuffer,
                    // ao, ssr, shadow, etc.) wired in via the render graph `inputs` map.
                    uint32_t extraBinding = 1u;
                    for (const auto& extra : extraInputs)
                    {
                        if (!extra)
                        {
                            ++extraBinding;
                            continue;
                        }
                        const auto extraDesc  = ctx.fg.getDescriptor<framegraph::FrameGraphTexture>(extra);
                        const bool depthInput = isDepthFormat(extraDesc.format);
                        (void)builder.read(
                            extra,
                            framegraph::TextureRead {
                                .binding =
                                    {
                                        .location      = {.set = 3, .binding = extraBinding},
                                        .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                    },
                                .type        = framegraph::TextureRead::Type::eCombinedImageSampler,
                                .imageAspect = depthInput ? rhi::ImageAspect::eDepth : rhi::ImageAspect::eColor,
                            });
                        ++extraBinding;
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
                [this, pushConstants = std::move(pushConstants)](
                    const PassData& data, FrameGraphPassResources&, void* ctxPtr) {
                    VULTRA_SCOPED_FRAMEGRAPH_EXEC_CONTEXT(rc, ctxPtr);
                    if (!m_VertexShaderLibrary || !m_FragmentShaderLibrary)
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
                    if (!pushConstants.empty())
                    {
                        rc.cb.pushConstants(rhi::ShaderStages::eFragment,
                                            0,
                                            static_cast<uint32_t>(pushConstants.size()),
                                            pushConstants.data());
                    }
                    rc.cb.beginRendering(framebufferInfo.value()).drawFullScreenTriangle().endRendering();
                });

            if (publishNamedOutput && output && !isBackbufferResource(m_Desc.output))
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

            auto vertexShader =
                loadShader(*m_VertexShaderLibrary, m_Desc.shader.vertex, vshadersystem::ShaderStage::eVert);
            auto fragmentShader =
                loadShader(*m_FragmentShaderLibrary, m_Desc.shader.fragment, vshadersystem::ShaderStage::eFrag);
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

        std::optional<rhi::ShaderLibraryRuntime::LoadedShader> loadShader(rhi::ShaderLibraryRuntime& shaderLibrary,
                                                                          const std::string&         shaderId,
                                                                          const vshadersystem::ShaderStage stage) const
        {
            std::vector<std::string> shaderIds {shaderId};
            if (shaderId.find('/') == std::string::npos && shaderId.find('\\') == std::string::npos)
                shaderIds.push_back("fullscreen/" + shaderId);

            std::optional<rhi::ShaderLibraryRuntime::LoadedShader> shader;
            for (const auto& id : shaderIds)
            {
                const auto hash = rhi::ShaderLibraryRuntime::computeVariantHash(id, stage, {});
                if (!shaderLibrary.hasVariant(hash, stage))
                    continue;
                shader = shaderLibrary.load(hash, stage);
                if (shader)
                    break;
            }
            if (!shader)
                VULTRA_CORE_ERROR(
                    "[DeclarativeRenderer] Failed to load shader '{}' for pass '{}'", shaderId, m_Desc.name);
            return shader;
        }

        std::vector<std::byte> makePushConstants(const vrendergraph::ParamBlock& params) const
        {
            if (!m_FragmentShaderLibrary)
                return {};
            auto shader =
                loadShader(*m_FragmentShaderLibrary, m_Desc.shader.fragment, vshadersystem::ShaderStage::eFrag);
            if (!shader)
                return {};
            return packShaderParams(shader->materialDesc, params);
        }

    private:
        FullscreenPass                                      m_Desc;
        rhi::ShaderLibraryRuntime*                          m_VertexShaderLibrary {nullptr};
        rhi::ShaderLibraryRuntime*                          m_FragmentShaderLibrary {nullptr};
        std::unordered_map<uint64_t, rhi::GraphicsPipeline> m_Pipelines;
    };

    // =====================================================================
    // Scripted render pass (Lua `setup` + `execute`) support - the standard for
    // project render passes.
    //
    // A scripted pass lets Lua drive the FrameGraph builder and the command
    // recorder directly through LuaPassBuildContext / LuaPassExecContext. The
    // closures live in DeclarativeRenderer::m_RenderScriptState (persistent), so
    // they stay valid across frames.
    // =====================================================================
    namespace
    {
        // Monotonic per-(pass,frame) tag used to reject FrameGraph handles that a
        // script stashed and tried to reuse outside the setup call that made them.
        std::atomic<uint64_t> s_ScriptedPassGeneration {0};
        // Thread that owns the render-script sol::state (Option A invariant:
        // framegraph execute, hence scripted execute, runs on this same thread).
        std::thread::id s_RenderScriptThreadId {};

        void assertRenderScriptThread()
        {
            assert((s_RenderScriptThreadId == std::thread::id {} ||
                    std::this_thread::get_id() == s_RenderScriptThreadId) &&
                   "scripted pass execute must run on the render-script thread");
        }

        void logScriptedPassError(const std::string& passType, const char* phase, const char* what)
        {
            static std::unordered_set<std::string> reported;
            const auto                             key = passType + "/" + phase;
            if (!reported.insert(key).second)
                return;
            VULTRA_CORE_ERROR("[DeclarativeRenderer] Scripted pass '{}' {} error: {}", passType, phase, what);
        }

        [[nodiscard]] vshadersystem::ShaderStage scriptedShaderStage(std::string_view s)
        {
            if (s == "vertex" || s == "vert")
                return vshadersystem::ShaderStage::eVert;
            if (s == "compute" || s == "comp")
                return vshadersystem::ShaderStage::eComp;
            return vshadersystem::ShaderStage::eFrag;
        }

        [[nodiscard]] rhi::ShaderStages scriptedRhiStage(std::string_view s)
        {
            if (s == "vertex" || s == "vert")
                return rhi::ShaderStages::eVertex;
            if (s == "compute" || s == "comp")
                return rhi::ShaderStages::eCompute;
            return rhi::ShaderStages::eFragment;
        }

        [[nodiscard]] framegraph::PipelineStage scriptedPipelineStage(std::string_view s)
        {
            if (s == "compute" || s == "comp")
                return framegraph::PipelineStage::eComputeShader;
            return framegraph::PipelineStage::eFragmentShader;
        }

        [[nodiscard]] rhi::PixelFormat scriptedPixelFormat(std::string_view s, rhi::PixelFormat fallback)
        {
            if (s == "rgba16f")
                return rhi::PixelFormat::eRGBA16F;
            if (s == "rgba32f")
                return rhi::PixelFormat::eRGBA32F;
            if (s == "rgba8" || s == "rgba8_unorm")
                return rhi::PixelFormat::eRGBA8_UNorm;
            return fallback;
        }

        [[nodiscard]] std::optional<rhi::ShaderLibraryRuntime::LoadedShader>
        loadScriptedShader(rhi::ShaderLibraryRuntime&       lib,
                           const std::string&               id,
                           const vshadersystem::ShaderStage stage)
        {
            std::vector<std::string> ids {id};
            if (id.find('/') == std::string::npos && id.find('\\') == std::string::npos)
                ids.push_back((stage == vshadersystem::ShaderStage::eComp ? std::string {"compute/"} :
                                                                            std::string {"fullscreen/"}) +
                              id);
            for (const auto& candidate : ids)
            {
                const auto hash = rhi::ShaderLibraryRuntime::computeVariantHash(candidate, stage, {});
                if (!lib.hasVariant(hash, stage))
                    continue;
                if (auto sh = lib.load(hash, stage))
                    return sh;
            }
            return std::nullopt;
        }

        // Converts a shader's reflected material parameters (the .vshader
        // [properties] block: name/type/default/range) into render-graph node
        // params, so the editor exposes them and they serialize into the .vrg.json.
        // packShaderParams then consumes the per-node overrides by name.
        [[nodiscard]] std::vector<vrendergraph::ParamDesc>
        paramsFromShaderReflection(const vshadersystem::MaterialDescription& md)
        {
            std::vector<vrendergraph::ParamDesc> out;
            out.reserve(md.params.size());
            for (const auto& p : md.params)
            {
                vrendergraph::ParamDesc pd;
                pd.name = p.name;
                switch (p.type)
                {
                    case vshadersystem::ParamType::eFloat:
                        pd.type         = vrendergraph::ParamType::eFloat;
                        pd.defaultValue = p.hasDefault ? shaderParamDefaultValue<float>(p.defaultValue) : 0.0f;
                        break;
                    case vshadersystem::ParamType::eInt:
                    case vshadersystem::ParamType::eUInt:
                        pd.type         = vrendergraph::ParamType::eInt;
                        pd.defaultValue = p.hasDefault ? shaderParamDefaultValue<int32_t>(p.defaultValue) : int32_t {0};
                        break;
                    case vshadersystem::ParamType::eBool:
                        pd.type         = vrendergraph::ParamType::eBoolean;
                        pd.defaultValue = p.hasDefault ? shaderParamDefaultValue<bool>(p.defaultValue) : false;
                        break;
                    default:
                        continue; // vec/mat params are not exposed as scalar node params
                }

                if (p.hasRange)
                {
                    if (pd.type == vrendergraph::ParamType::eFloat)
                    {
                        pd.minValue = nlohmann::json(static_cast<float>(p.range.min));
                        pd.maxValue = nlohmann::json(static_cast<float>(p.range.max));
                    }
                    else if (pd.type == vrendergraph::ParamType::eInt)
                    {
                        pd.minValue = nlohmann::json(static_cast<int32_t>(p.range.min));
                        pd.maxValue = nlohmann::json(static_cast<int32_t>(p.range.max));
                    }
                }
                else if (!p.enumOptions.empty() && pd.type == vrendergraph::ParamType::eInt)
                {
                    int32_t lo = p.enumOptions.front().value;
                    int32_t hi = lo;
                    for (const auto& o : p.enumOptions)
                    {
                        lo = std::min(lo, o.value);
                        hi = std::max(hi, o.value);
                    }
                    pd.minValue = nlohmann::json(lo);
                    pd.maxValue = nlohmann::json(hi);
                }

                out.push_back(std::move(pd));
            }
            return out;
        }

        // Reads push-constant values directly from a Lua table keyed by the
        // shader-reflected parameter names. Mirrors packShaderParams but sources
        // values from Lua instead of a vrendergraph::ParamBlock.
        [[nodiscard]] std::vector<std::byte>
        packShaderParamsFromLua(const vshadersystem::MaterialDescription& materialDesc, sol::table values)
        {
            if (materialDesc.materialParamSize == 0u || materialDesc.params.empty())
                return {};

            std::vector<std::byte> bytes(materialDesc.materialParamSize);
            for (const auto& param : materialDesc.params)
            {
                sol::object v = values[param.name];
                switch (param.type)
                {
                    case vshadersystem::ParamType::eFloat: {
                        const float fb = param.hasDefault ? shaderParamDefaultValue<float>(param.defaultValue) : 0.0f;
                        writePushConstantValue(bytes, param, v.is<float>() ? v.as<float>() : fb);
                        break;
                    }
                    case vshadersystem::ParamType::eInt: {
                        const int32_t fb =
                            param.hasDefault ? shaderParamDefaultValue<int32_t>(param.defaultValue) : int32_t {0};
                        writePushConstantValue(bytes, param, v.is<int>() ? v.as<int>() : fb);
                        break;
                    }
                    case vshadersystem::ParamType::eUInt: {
                        const uint32_t fb =
                            param.hasDefault ? shaderParamDefaultValue<uint32_t>(param.defaultValue) : uint32_t {0};
                        const uint32_t val = v.is<int>() ? static_cast<uint32_t>(std::max(v.as<int>(), 0)) : fb;
                        writePushConstantValue(bytes, param, val);
                        break;
                    }
                    case vshadersystem::ParamType::eBool: {
                        const int32_t fb =
                            param.hasDefault && shaderParamDefaultValue<bool>(param.defaultValue) ? 1 : 0;
                        const int32_t val = v.is<bool>() ? (v.as<bool>() ? 1 : 0) : fb;
                        writePushConstantValue(bytes, param, val);
                        break;
                    }
                    default:
                        break;
                }
            }
            return bytes;
        }

        // A FrameGraph resource handle exposed to Lua, tagged with the generation
        // of the setup call that produced it.
        struct LuaResHandle
        {
            FrameGraphResource resource {};
            uint64_t           generation {0};
        };

        struct ScriptedShaderSelection
        {
            bool                       compute {false};
            rhi::ShaderLibraryRuntime* vertexLib {nullptr};
            rhi::ShaderLibraryRuntime* fragmentLib {nullptr};
            rhi::ShaderLibraryRuntime* computeLib {nullptr};
            std::string                vertexId;
            std::string                fragmentId;
            std::string                computeId;
            bool                       valid {false};
        };

        // Per-frame state carried from a scripted pass's setup to its execute.
        struct ScriptedPassUpscalerData
        {
            bool enabled {false};
            FrameGraphResource color;
            FrameGraphResource output;
            FrameGraphResource depth;
            FrameGraphResource motion;
            FrameGraphResource exposure;
            // Stereo: dedicated single-layer per-eye outputs (providers cannot address array
            // layers); the evaluate step blits them into the layered output.
            FrameGraphResource eyeOutputs[2];
            bool hasDepth {false};
            bool hasMotion {false};
            bool hasExposure {false};
            bool hasEyeOutputs {false};
        };

        struct ScriptedPassFrameData
        {
            ScriptedShaderSelection shader;
            // Extent of the last created output texture, used by dispatchByOutputSize().
            rhi::Extent2D outputExtent {0, 0};
            ScriptedPassUpscalerData upscaler;
        };

        struct ScriptedPassEnv
        {
            IShaderService*                                     shaderService {nullptr};
            const std::unordered_map<std::string, std::string>* shaderLibraries {nullptr};
            std::string                                         sourcePath; // pass .lua, for diagnostics
        };

        // Persistent (per pass type) pipeline cache for scripted passes.
        class ScriptedPassPipelines
        {
        public:
            void invalidate()
            {
                m_Graphics.clear();
                m_Compute.reset();
            }

            rhi::GraphicsPipeline* getGraphics(rhi::RenderDevice&             rd,
                                               const ScriptedShaderSelection& sel,
                                               const rhi::PixelFormat         colorFormat,
                                               const uint32_t                 viewMask)
            {
                if (!sel.vertexLib || !sel.fragmentLib)
                    return nullptr;

                const uint64_t key = static_cast<uint64_t>(colorFormat) | (static_cast<uint64_t>(viewMask) << 32u);
                if (auto it = m_Graphics.find(key); it != m_Graphics.end())
                    return &it->second;

                auto vert = loadScriptedShader(*sel.vertexLib, sel.vertexId, vshadersystem::ShaderStage::eVert);
                auto frag = loadScriptedShader(*sel.fragmentLib, sel.fragmentId, vshadersystem::ShaderStage::eFrag);
                if (!vert || !frag)
                    return nullptr;

                auto builder = rhi::GraphicsPipeline::Builder {};
                builder.setColorFormats({colorFormat})
                    .setViewMask(viewMask)
                    .setInputAssembly({})
                    .setDepthStencil({.depthTest = false, .depthWrite = false})
                    .setRasterizer({.polygonMode = rhi::PolygonMode::eFill, .cullMode = rhi::CullMode::eNone})
                    .setBlending(0, {.enabled = false});

                if (rd.getBackendApi() == rhi::RenderBackendApi::eWebGPU)
                {
                    builder
                        .addShader(rhi::ShaderType::eVertex,
                                   {.code           = vert->wgsl,
                                    .entryPointName = "main",
                                    .defines        = {},
                                    .reflection     = vert->reflection})
                        .addShader(rhi::ShaderType::eFragment,
                                   {.code           = frag->wgsl,
                                    .entryPointName = "main",
                                    .defines        = {},
                                    .reflection     = frag->reflection});
                }
                else
                {
                    builder.addBuiltinShader(rhi::ShaderType::eVertex, *vert)
                        .addBuiltinShader(rhi::ShaderType::eFragment, *frag);
                }

                auto [it, inserted] = m_Graphics.emplace(key, builder.build(rd));
                static_cast<void>(inserted);
                return &it->second;
            }

            rhi::ComputePipeline* getCompute(rhi::RenderDevice& rd, const ScriptedShaderSelection& sel)
            {
                if (m_Compute)
                    return &m_Compute.value();
                if (!sel.computeLib)
                    return nullptr;
                auto comp = loadScriptedShader(*sel.computeLib, sel.computeId, vshadersystem::ShaderStage::eComp);
                if (!comp)
                    return nullptr;
                m_Compute = rd.createComputePipelineBuiltin(*comp);
                return &m_Compute.value();
            }

        private:
            std::unordered_map<uint64_t, rhi::GraphicsPipeline> m_Graphics;
            std::optional<rhi::ComputePipeline>                 m_Compute;
        };

        // -------- Lua-facing build context (valid only during setup) ----------
        class LuaPassBuildContext
        {
        public:
            LuaPassBuildContext(FrameGraph::Builder&            builder,
                                FrameGraphBuildContext&         ctx,
                                vrendergraph::PassBuildContext& passCtx,
                                const vrendergraph::ParamBlock& params,
                                ScriptedPassFrameData&          frame,
                                ScriptedPassEnv                 env,
                                const uint64_t                  generation) :
                m_Builder(&builder), m_Ctx(&ctx), m_PassCtx(&passCtx), m_Params(&params), m_Frame(&frame), m_Env(env),
                m_Generation(generation)
            {}

            LuaResHandle getInput(const std::string& slot) { return make(m_PassCtx->getInput(slot)); }
            void         setOutput(const std::string& slot, const LuaResHandle& h)
            {
                check(h);
                m_PassCtx->setOutput(slot, h.resource);
            }

            sol::object getResource(const std::string& name, sol::this_state s)
            {
                const auto res = m_Ctx->data.tryGet(resourceKeyFor(name));
                if (!res)
                    return sol::nil;
                return sol::make_object(s, make(res));
            }
            void setResource(const std::string& name, const LuaResHandle& h)
            {
                check(h);
                m_Ctx->data.set(resourceKeyFor(name), h.resource);
            }

            LuaResHandle createColorTexture(sol::table opts)
            {
                const std::string name    = getString(opts, "name", "ScriptedPass Color");
                const bool        storage = getBool(opts, "storage", false);
                auto              usage   = rhi::ImageUsage::eSampled | rhi::ImageUsage::eTransferSrc;
                if (storage)
                    usage = usage | rhi::ImageUsage::eStorage;

                framegraph::FrameGraphTexture::Desc desc;
                sol::object                         inheritObj = opts["inherit"];
                if (inheritObj.is<LuaResHandle>())
                {
                    const auto inherit     = inheritObj.as<LuaResHandle>();
                    const auto inheritDesc = m_Ctx->fg.getDescriptor<framegraph::FrameGraphTexture>(inherit.resource);
                    const auto fmt         = scriptedPixelFormat(getString(opts, "format", ""), inheritDesc.format);
                    desc                   = makeInheritedTextureDesc(inheritDesc, fmt, usage);
                }
                else
                {
                    const auto fmt =
                        scriptedPixelFormat(getString(opts, "format", "rgba16f"), rhi::PixelFormat::eRGBA16F);
                    desc = makeRenderViewTextureDesc(m_Ctx->view(), fmt, usage);
                }
                m_Frame->outputExtent = desc.extent;
                return make(m_Builder->create<framegraph::FrameGraphTexture>(name, desc));
            }

            LuaResHandle createUpscalerOutput(sol::table opts)
            {
                sol::object colorObj = opts["color"];
                if (!colorObj.is<LuaResHandle>())
                    return {};

                const auto color = colorObj.as<LuaResHandle>();
                check(color);

                ScriptedPassUpscalerData upscaler {};
                upscaler.enabled = true;
                upscaler.color   = m_Builder->read(
                    color.resource,
                    framegraph::TextureRead {
                        .binding =
                            {
                                .location      = {.set = 0, .binding = 0},
                                .pipelineStage = framegraph::PipelineStage::eComputeShader,
                            },
                        .type        = framegraph::TextureRead::Type::eSampledImage,
                        .imageAspect = rhi::ImageAspect::eColor,
                    });

                const auto readOptional = [&](const char* key,
                                              const uint32_t binding,
                                              const rhi::ImageAspect aspect,
                                              FrameGraphResource& out,
                                              bool& has) {
                    sol::object obj = opts[key];
                    if (!obj.is<LuaResHandle>())
                        return;
                    const auto handle = obj.as<LuaResHandle>();
                    check(handle);
                    out = m_Builder->read(
                        handle.resource,
                        framegraph::TextureRead {
                            .binding =
                                {
                                    .location      = {.set = 0, .binding = binding},
                                    .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                },
                            .type        = framegraph::TextureRead::Type::eSampledImage,
                            .imageAspect = aspect,
                        });
                    has = true;
                };
                readOptional("depth", 1, rhi::ImageAspect::eDepth, upscaler.depth, upscaler.hasDepth);
                readOptional("motion", 2, rhi::ImageAspect::eColor, upscaler.motion, upscaler.hasMotion);
                readOptional("exposure", 3, rhi::ImageAspect::eColor, upscaler.exposure, upscaler.hasExposure);

                const auto sourceDesc = m_Ctx->fg.getDescriptor<framegraph::FrameGraphTexture>(color.resource);
                auto       outputDesc = makeInheritedTextureDesc(
                    sourceDesc,
                    sourceDesc.format,
                    rhi::ImageUsage::eStorage | rhi::ImageUsage::eSampled | rhi::ImageUsage::eTransferSrc |
                        rhi::ImageUsage::eTransferDst);
                const uint32_t outputWidth  = static_cast<uint32_t>(std::max(getInt(opts, "outputWidth", 0), 0));
                const uint32_t outputHeight = static_cast<uint32_t>(std::max(getInt(opts, "outputHeight", 0), 0));
                if (outputWidth > 0u && outputHeight > 0u)
                    outputDesc.extent = {outputWidth, outputHeight};
                else if (auto* target = m_Ctx->view().target; target != nullptr)
                    outputDesc.extent = target->getExtent();

                const std::string name = getString(opts, "name", "Upscaler Output");
                auto output = m_Builder->create<framegraph::FrameGraphTexture>(name, outputDesc);
                upscaler.output = m_Builder->write(
                    output,
                    framegraph::ImageWrite {
                        .binding =
                            {
                                .location      = {.set = 0, .binding = 4},
                                .pipelineStage = framegraph::PipelineStage::eComputeShader,
                            },
                        .imageAspect = rhi::ImageAspect::eColor,
                    });

                // Stereo: providers cannot address array layers, so each eye evaluates into a
                // dedicated single-layer staging that is blitted into the layered output.
                if (m_Ctx->view().usesSingleGraphStereo() && sourceDesc.layers >= 2u)
                {
                    auto eyeDesc       = outputDesc;
                    eyeDesc.layers     = 0u;
                    eyeDesc.viewMask   = 0u;
                    eyeDesc.usageFlags = rhi::ImageUsage::eStorage | rhi::ImageUsage::eSampled |
                                         rhi::ImageUsage::eTransferSrc | rhi::ImageUsage::eTransferDst;
                    for (uint32_t eye = 0; eye < 2u; ++eye)
                    {
                        auto eyeOutput = m_Builder->create<framegraph::FrameGraphTexture>(
                            name + (eye == 0u ? " Eye0" : " Eye1"), eyeDesc);
                        upscaler.eyeOutputs[eye] = m_Builder->write(
                            eyeOutput,
                            framegraph::ImageWrite {
                                .binding =
                                    {
                                        .location      = {.set = 0, .binding = 5u + eye},
                                        .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                    },
                                .imageAspect = rhi::ImageAspect::eColor,
                            });
                    }
                    upscaler.hasEyeOutputs = true;
                }

                m_Frame->outputExtent = outputDesc.extent;
                m_Frame->upscaler     = upscaler;
                return make(upscaler.output);
            }

            void read(const LuaResHandle& h, sol::table binding)
            {
                check(h);
                const uint32_t set       = static_cast<uint32_t>(getInt(binding, "set", 3));
                const uint32_t bindingIx = static_cast<uint32_t>(getInt(binding, "binding", 0));
                const auto     stageName = getString(binding, "stage", "fragment");
                const auto     stage     = scriptedPipelineStage(stageName);
                const bool     depth     = getBool(binding, "depth", false);
                // Compute shaders sample via texelFetch (sampled image); fragment
                // shaders use a combined image sampler. Match the existing runtimes.
                const bool isCompute = stage == framegraph::PipelineStage::eComputeShader;
                (void)m_Builder->read(
                    h.resource,
                    framegraph::TextureRead {
                        .binding     = {.location = {.set = set, .binding = bindingIx}, .pipelineStage = stage},
                        .type        = isCompute ? framegraph::TextureRead::Type::eSampledImage :
                                                   framegraph::TextureRead::Type::eCombinedImageSampler,
                        .imageAspect = depth ? rhi::ImageAspect::eDepth : rhi::ImageAspect::eColor,
                    });
            }

            void writeColor(const LuaResHandle& h, sol::optional<int> index, sol::optional<bool> clear)
            {
                check(h);
                (void)m_Builder->write(
                    h.resource,
                    framegraph::Attachment {
                        .index       = static_cast<uint32_t>(index.value_or(0)),
                        .imageAspect = rhi::ImageAspect::eColor,
                        .clearValue = clear.value_or(false) ?
                                          std::optional<framegraph::ClearValue> {framegraph::ClearValue::eOpaqueBlack} :
                                          std::optional<framegraph::ClearValue> {},
                    });
            }

            void writeStorage(const LuaResHandle& h, sol::table binding)
            {
                check(h);
                const uint32_t set       = static_cast<uint32_t>(getInt(binding, "set", 3));
                const uint32_t bindingIx = static_cast<uint32_t>(getInt(binding, "binding", 0));
                const auto     stage     = scriptedPipelineStage(getString(binding, "stage", "compute"));
                (void)m_Builder->write(
                    h.resource,
                    framegraph::ImageWrite {
                        .binding     = {.location = {.set = set, .binding = bindingIx}, .pipelineStage = stage},
                        .imageAspect = rhi::ImageAspect::eColor,
                    });
            }

            void useGraphicsShader(sol::table t)
            {
                ScriptedShaderSelection sel;
                sel.compute                = false;
                const auto library         = getString(t, "library", "project");
                const auto vertexLibrary   = getString(t, "vertexLibrary", "builtin");
                const auto fragmentLibrary = getString(t, "fragmentLibrary", library);
                sel.vertexId               = getString(t, "vertex", "builtin/general/fullscreen_triangle.vert");
                sel.fragmentId             = getString(t, "fragment", "");
                sel.vertexLib              = resolveLibrary(vertexLibrary);
                sel.fragmentLib            = resolveLibrary(fragmentLibrary);
                sel.valid                  = sel.vertexLib && sel.fragmentLib && !sel.fragmentId.empty();
                reportShaderDiagnostics({
                    {vertexLibrary, sel.vertexId, sel.vertexLib, vshadersystem::ShaderStage::eVert, "vertex"},
                    {fragmentLibrary, sel.fragmentId, sel.fragmentLib, vshadersystem::ShaderStage::eFrag, "fragment"},
                });
                m_Frame->shader = std::move(sel);
            }

            void useComputeShader(sol::table t)
            {
                ScriptedShaderSelection sel;
                sel.compute        = true;
                const auto library = getString(t, "library", "project");
                sel.computeId      = getString(t, "compute", "");
                sel.computeLib     = resolveLibrary(library);
                sel.valid          = sel.computeLib && !sel.computeId.empty();
                reportShaderDiagnostics(
                    {{library, sel.computeId, sel.computeLib, vshadersystem::ShaderStage::eComp, "compute"}});
                m_Frame->shader = std::move(sel);
            }

            float       paramFloat(const std::string& n, float d) const { return m_Params->get<float>(n, d); }
            int         paramInt(const std::string& n, int d) const { return m_Params->get<int>(n, d); }
            bool        paramBool(const std::string& n, bool d) const { return m_Params->get<bool>(n, d); }
            std::string paramString(const std::string& n, std::string d) const
            {
                return m_Params->get<std::string>(n, std::move(d));
            }

            uint32_t sceneDrawCount() const
            {
                auto* v = m_Ctx->view().gpuSceneView;
                return v ? v->getDispatchableDrawCount() : 0u;
            }
            bool hasGaussianSplats() const
            {
                auto* v = m_Ctx->view().gpuSceneView;
                return v ? v->hasGeneralGaussianSplats() : false;
            }
            bool isGpuDriven() const
            {
                auto* v = m_Ctx->view().gpuSceneView;
                return v ? v->isGpuDriven() : false;
            }

        private:
            struct ShaderProbe
            {
                std::string                name; // library name (for the message)
                std::string                id;
                rhi::ShaderLibraryRuntime* lib;
                vshadersystem::ShaderStage stage;
                const char*                role;
            };

            // Validate the shaders a scripted pass selected in setup and publish
            // any "not found" markers against the pass's source .lua so the code
            // editor can show them. Passing an all-resolved set clears prior markers.
            void reportShaderDiagnostics(std::initializer_list<ShaderProbe> probes)
            {
                if (!m_Env.shaderService || m_Env.sourcePath.empty())
                    return;
                std::vector<AssetDiagnostic> diagnostics;
                for (const auto& probe : probes)
                {
                    if (probe.id.empty())
                    {
                        diagnostics.push_back({m_Env.sourcePath,
                                               0,
                                               0,
                                               std::string {"Scripted pass: no "} + probe.role + " shader specified."});
                        continue;
                    }
                    const auto hash = rhi::ShaderLibraryRuntime::computeVariantHash(probe.id, probe.stage, {});
                    if (!probe.lib || !probe.lib->hasVariant(hash, probe.stage))
                        diagnostics.push_back({m_Env.sourcePath,
                                               0,
                                               0,
                                               std::string {"Scripted pass: "} + probe.role + " shader '" + probe.id +
                                                   "' not found in library '" + probe.name + "'."});
                }
                m_Env.shaderService->setRenderPassDiagnostics(m_Env.sourcePath, std::move(diagnostics));
            }

            rhi::ShaderLibraryRuntime* resolveLibrary(const std::string& name) const
            {
                if (!m_Env.shaderService)
                    return nullptr;
                if (name == "builtin")
                    return &m_Env.shaderService->builtinLibrary();
                if (m_Env.shaderLibraries)
                {
                    if (auto it = m_Env.shaderLibraries->find(name); it != m_Env.shaderLibraries->end())
                        return m_Env.shaderService->findProjectLibrary(it->second);
                }
                return nullptr;
            }

            LuaResHandle make(FrameGraphResource r) const { return LuaResHandle {r, m_Generation}; }
            void         check(const LuaResHandle& h) const
            {
                if (h.generation != m_Generation)
                    throw std::runtime_error("scripted pass: stale FrameGraph handle used outside its setup frame");
            }

            FrameGraph::Builder*            m_Builder;
            FrameGraphBuildContext*         m_Ctx;
            vrendergraph::PassBuildContext* m_PassCtx;
            const vrendergraph::ParamBlock* m_Params;
            ScriptedPassFrameData*          m_Frame;
            ScriptedPassEnv                 m_Env;
            uint64_t                        m_Generation;
        };

        // -------- Lua-facing execute context (valid only during execute) ------
        class LuaPassExecContext
        {
        public:
            LuaPassExecContext(FrameGraphExecContext&       rc,
                               FrameGraphPassResources&     resources,
                               vbase::ServiceRegistry*      services,
                               const ScriptedPassFrameData& frame,
                               ScriptedPassPipelines&       pipelines) :
                m_Rc(&rc), m_Resources(&resources), m_Services(services), m_Frame(&frame), m_Pipelines(&pipelines)
            {}

            bool bindPipeline()
            {
                const auto& sel = m_Frame->shader;
                if (!sel.valid)
                    return false;

                if (sel.compute)
                {
                    auto* p = m_Pipelines->getCompute(m_Rc->rd, sel);
                    if (!p)
                        return false;
                    m_Rc->cb.bindPipeline(*p);
                    m_CurrentPipeline     = p;
                    m_ComputeLocalSize    = p->getWorkGroupSize();
                    m_CurrentMaterialDesc = materialDescFor(sel);
                    return true;
                }

                const auto fb = m_Rc->framebufferInfo();
                if (!fb)
                    return false;
                auto* p = m_Pipelines->getGraphics(m_Rc->rd, sel, rhi::getColorFormat(fb.value(), 0), fb->viewMask);
                if (!p)
                    return false;
                m_Rc->cb.bindPipeline(*p);
                m_CurrentPipeline     = p;
                m_CurrentMaterialDesc = materialDescFor(sel);
                return true;
            }

            void bindDescriptorSets()
            {
                if (!m_CurrentPipeline)
                    return;
                if (m_Rc->resourceSet.contains(3) && m_Rc->resourceSet[3].contains(0) &&
                    m_Rc->ext.samplers.contains("linear"))
                    m_Rc->overrideSampler(m_Rc->resourceSet[3][0], m_Rc->ext.samplers["linear"]);
                m_Rc->bindDescriptorSets(*m_CurrentPipeline);
            }

            void pushConstants(const std::string& stage, sol::table values)
            {
                if (!m_CurrentMaterialDesc)
                    return;
                const auto bytes = packShaderParamsFromLua(*m_CurrentMaterialDesc, values);
                if (bytes.empty())
                    return;
                m_Rc->cb.pushConstants(scriptedRhiStage(stage), 0, static_cast<uint32_t>(bytes.size()), bytes.data());
            }

            void beginRendering()
            {
                const auto fb = m_Rc->framebufferInfo();
                if (fb)
                    m_Rc->cb.beginRendering(fb.value());
            }
            void drawFullscreen() { m_Rc->cb.drawFullScreenTriangle(); }
            void endRendering() { m_Rc->cb.endRendering(); }

            void dispatch(uint32_t x, uint32_t y, uint32_t z)
            {
                m_Rc->cb.dispatch(glm::uvec3 {std::max(x, 1u), std::max(y, 1u), std::max(z, 1u)});
            }

            // Dispatch one workgroup per output texel block, using the bound compute
            // pipeline's local size and the extent of the last created output.
            void dispatchByOutputSize()
            {
                const auto     ext = m_Frame->outputExtent;
                const uint32_t lx  = std::max(m_ComputeLocalSize.x, 1u);
                const uint32_t ly  = std::max(m_ComputeLocalSize.y, 1u);
                const uint32_t gx  = (std::max(ext.width, 1u) + lx - 1u) / lx;
                const uint32_t gy  = (std::max(ext.height, 1u) + ly - 1u) / ly;
                m_Rc->cb.dispatch(glm::uvec3 {gx, gy, 1u});
            }

            bool evaluateUpscaler()
            {
                if (!m_Frame->upscaler.enabled || m_Resources == nullptr)
                    return false;

                auto* upscaler = m_Services ? m_Services->tryGet<IRenderUpscalerService>() : nullptr;
                if (upscaler == nullptr)
                    return blitUpscalerFallback();

                const auto settings = upscaler->settings();
                if (!settings.enabled || settings.mode == UpscalerMode::eOff || upscaler->activeProvider() == nullptr)
                    return blitUpscalerFallback();

                auto* inputTexture = m_Resources->get<framegraph::FrameGraphTexture>(m_Frame->upscaler.color).texture;
                auto* outputTexture =
                    m_Resources->get<framegraph::FrameGraphTexture>(m_Frame->upscaler.output).texture;
                if (inputTexture == nullptr || outputTexture == nullptr)
                    return false;
                if (m_Rc->view().camera != nullptr && !m_Rc->view().camera->allowUpscaler)
                    return blitUpscalerFallback();

                const UpscalerEvaluateTextures textures {
                    .color  = inputTexture,
                    .output = outputTexture,
                    .depth  = m_Frame->upscaler.hasDepth ?
                                  m_Resources->get<framegraph::FrameGraphTexture>(m_Frame->upscaler.depth).texture :
                                  nullptr,
                    .motion = m_Frame->upscaler.hasMotion ?
                                  m_Resources->get<framegraph::FrameGraphTexture>(m_Frame->upscaler.motion).texture :
                                  nullptr,
                    .exposure =
                        m_Frame->upscaler.hasExposure ?
                            m_Resources->get<framegraph::FrameGraphTexture>(m_Frame->upscaler.exposure).texture :
                            nullptr,
                    .eyeOutputs =
                        {
                            m_Frame->upscaler.hasEyeOutputs ?
                                m_Resources->get<framegraph::FrameGraphTexture>(m_Frame->upscaler.eyeOutputs[0])
                                    .texture :
                                nullptr,
                            m_Frame->upscaler.hasEyeOutputs ?
                                m_Resources->get<framegraph::FrameGraphTexture>(m_Frame->upscaler.eyeOutputs[1])
                                    .texture :
                                nullptr,
                        },
                };

                if (!evaluateUpscalerForView(*m_Rc, *upscaler, settings, textures))
                    return blitUpscalerFallback();

                m_Rc->clear();
                return true;
            }

        private:
            bool blitUpscalerFallback()
            {
                if (m_Resources == nullptr || !m_Frame->upscaler.enabled)
                    return false;
                auto* inputTexture = m_Resources->get<framegraph::FrameGraphTexture>(m_Frame->upscaler.color).texture;
                auto* outputTexture =
                    m_Resources->get<framegraph::FrameGraphTexture>(m_Frame->upscaler.output).texture;
                if (inputTexture == nullptr || outputTexture == nullptr)
                    return false;
                m_Rc->cb.blit(*inputTexture, *outputTexture, rhi::TexelFilter::eLinear);
                m_Rc->clear();
                return true;
            }

            std::optional<vshadersystem::MaterialDescription> materialDescFor(const ScriptedShaderSelection& sel) const
            {
                if (sel.compute)
                {
                    if (!sel.computeLib)
                        return std::nullopt;
                    if (auto sh = loadScriptedShader(*sel.computeLib, sel.computeId, vshadersystem::ShaderStage::eComp))
                        return sh->materialDesc;
                    return std::nullopt;
                }
                if (!sel.fragmentLib)
                    return std::nullopt;
                if (auto sh = loadScriptedShader(*sel.fragmentLib, sel.fragmentId, vshadersystem::ShaderStage::eFrag))
                    return sh->materialDesc;
                return std::nullopt;
            }

            FrameGraphExecContext*                            m_Rc;
            FrameGraphPassResources*                          m_Resources;
            vbase::ServiceRegistry*                           m_Services;
            const ScriptedPassFrameData*                      m_Frame;
            ScriptedPassPipelines*                            m_Pipelines;
            rhi::BasePipeline*                                m_CurrentPipeline {nullptr};
            glm::uvec3                                        m_ComputeLocalSize {1, 1, 1};
            std::optional<vshadersystem::MaterialDescription> m_CurrentMaterialDesc;
        };

        void registerScriptedPassLuaBindings(sol::state& lua)
        {
            lua.new_usertype<LuaResHandle>("VultraFrameGraphResource", sol::no_constructor);

            lua.new_usertype<LuaPassBuildContext>("VultraPassBuildContext",
                                                  sol::no_constructor,
                                                  "getInput",
                                                  &LuaPassBuildContext::getInput,
                                                  "setOutput",
                                                  &LuaPassBuildContext::setOutput,
                                                  "getResource",
                                                  &LuaPassBuildContext::getResource,
                                                  "setResource",
                                                  &LuaPassBuildContext::setResource,
                                                  "createColorTexture",
                                                  &LuaPassBuildContext::createColorTexture,
                                                  "createUpscalerOutput",
                                                  &LuaPassBuildContext::createUpscalerOutput,
                                                  "read",
                                                  &LuaPassBuildContext::read,
                                                  "writeColor",
                                                  &LuaPassBuildContext::writeColor,
                                                  "writeStorage",
                                                  &LuaPassBuildContext::writeStorage,
                                                  "useGraphicsShader",
                                                  &LuaPassBuildContext::useGraphicsShader,
                                                  "useComputeShader",
                                                  &LuaPassBuildContext::useComputeShader,
                                                  "paramFloat",
                                                  &LuaPassBuildContext::paramFloat,
                                                  "paramInt",
                                                  &LuaPassBuildContext::paramInt,
                                                  "paramBool",
                                                  &LuaPassBuildContext::paramBool,
                                                  "paramString",
                                                  &LuaPassBuildContext::paramString,
                                                  "sceneDrawCount",
                                                  &LuaPassBuildContext::sceneDrawCount,
                                                  "hasGaussianSplats",
                                                  &LuaPassBuildContext::hasGaussianSplats,
                                                  "isGpuDriven",
                                                  &LuaPassBuildContext::isGpuDriven);

            lua.new_usertype<LuaPassExecContext>("VultraPassExecContext",
                                                 sol::no_constructor,
                                                 "bindPipeline",
                                                 &LuaPassExecContext::bindPipeline,
                                                 "bindDescriptorSets",
                                                 &LuaPassExecContext::bindDescriptorSets,
                                                 "pushConstants",
                                                 &LuaPassExecContext::pushConstants,
                                                 "beginRendering",
                                                 &LuaPassExecContext::beginRendering,
                                                 "drawFullscreen",
                                                 &LuaPassExecContext::drawFullscreen,
                                                 "endRendering",
                                                 &LuaPassExecContext::endRendering,
                                                 "dispatch",
                                                 &LuaPassExecContext::dispatch,
                                                 "evaluateUpscaler",
                                                 &LuaPassExecContext::evaluateUpscaler,
                                                 "dispatchByOutputSize",
                                                 &LuaPassExecContext::dispatchByOutputSize);
        }

        // Parses a scripted pass `params` list (array of {name,type,default}).
        [[nodiscard]] std::vector<vrendergraph::ParamDesc> parseScriptedPassParams(sol::table table)
        {
            std::vector<vrendergraph::ParamDesc> out;
            sol::object                          paramsObj = table["params"];
            if (!paramsObj.is<sol::table>())
                return out;

            sol::table params = paramsObj.as<sol::table>();
            for (const auto& [_, entryObj] : params)
            {
                static_cast<void>(_);
                if (!entryObj.is<sol::table>())
                    continue;
                sol::table entry = entryObj.as<sol::table>();
                const auto name  = getString(entry, "name");
                if (name.empty())
                    continue;
                const auto              type = normalizeRenderGraphId(getString(entry, "type", "float"));
                sol::object             def  = entry["default"];
                vrendergraph::ParamDesc pd;
                pd.name = name;
                if (type == "int")
                {
                    pd.type         = vrendergraph::ParamType::eInt;
                    pd.defaultValue = getInt(entry, "default", 0);
                }
                else if (type == "bool" || type == "boolean")
                {
                    pd.type         = vrendergraph::ParamType::eBoolean;
                    pd.defaultValue = getBool(entry, "default", false);
                }
                else if (type == "string")
                {
                    pd.type         = vrendergraph::ParamType::eString;
                    pd.defaultValue = getString(entry, "default", "");
                }
                else
                {
                    pd.type         = vrendergraph::ParamType::eFloat;
                    pd.defaultValue = def.is<double>() ? static_cast<float>(def.as<double>()) : 0.0f;
                }
                out.push_back(std::move(pd));
            }
            return out;
        }
    } // namespace

    class DeclarativeRenderer::RenderGraphRuntime
    {
    public:
        RenderGraphRuntime(DeclarativeRenderer& owner, std::string uri, vrendergraph::RenderGraphDesc desc) :
            m_Owner(owner), m_Uri(std::move(uri)), m_Desc(std::move(desc)), m_Host(owner)
        {
            m_PrefersExplicitPerEye = graphPrefersExplicitPerEye(m_Desc);
            registerPasses();
            registerResources();
        }

        [[nodiscard]] std::string_view uri() const { return m_Uri; }

        // True when this graph names the two XR eyes itself (see graphPrefersExplicitPerEye);
        // render_system reads this to skip forcing single-graph multiview.
        [[nodiscard]] bool prefersExplicitPerEye() const { return m_PrefersExplicitPerEye; }

        void updateDesc(vrendergraph::RenderGraphDesc desc)
        {
            m_Desc                  = std::move(desc);
            m_PrefersExplicitPerEye = graphPrefersExplicitPerEye(m_Desc);
            m_LastValidationError.clear();

            // Warn (non-fatal) if the graph is structurally incomplete. Branching is now explicit
            // $-logic wiring, so a structurally-complete graph is complete on every capability profile;
            // authors catch holes at load time rather than as a black frame on the target device.
            vrendergraph::RenderGraphDesc check = m_Desc;
            materializeRenderGraphDefaultOutputs(m_Registry, check);
            if (std::string capError; !validateRenderGraphStructure(check, capError))
                VULTRA_CORE_WARN("[DeclarativeRenderer] Render graph '{}' is incomplete: {}", m_Uri, capError);
        }

        void invalidateShaderPipelines()
        {
            for (auto& [_, pipelines] : m_ScriptedPassPipelines)
            {
                if (pipelines)
                    pipelines->invalidate();
            }
        }

        void build(FrameGraphBuildContext& ctx)
        {
            RenderGraphCapabilities caps;
            caps.backend       = ctx.rd.getBackendApi();
            caps.features      = ctx.rd.getFeatureFlag();
            caps.featureReport = ctx.rd.getFeatureReport().flags;
#if defined(__ANDROID__)
            caps.platformAndroid = true;
#endif
            caps.platformWeb = caps.backend == rhi::RenderBackendApi::eWebGPU;
            // Single source of truth for the active tier: web/android force compat; on desktop the
            // owner relays the renderer's resolved compat decision (e.g. --render-profile=compat).
            caps.tierHighend = !caps.platformAndroid && !caps.platformWeb && !m_Owner.m_ForceCompatTier;

            // Drop inactive passes (disabled / wrong viewMode) and pass through their consumers. The
            // remaining graph - including its $-logic nodes - is handed to vrendergraph, which evaluates
            // the value/logic DAG and culls unselected branches itself (see RenderGraph::build).
            vrendergraph::RenderGraphDesc activeDesc = makeActiveGraphWithPassthrough(m_Desc, ctx.view());
            materializeRenderGraphDefaultOutputs(m_Registry, activeDesc);
            if (activeDesc.passes.empty())
                return;

            std::string topoError;
            if (!applyRenderGraphTopoOrder(activeDesc, &topoError))
            {
                if (topoError != m_LastValidationError)
                {
                    VULTRA_CORE_ERROR("[DeclarativeRenderer] Invalid render graph '{}': {}", m_Uri, topoError);
                    m_LastValidationError = topoError;
                }
                return;
            }

            std::string validationError;
            if (!validateRenderGraphStructure(activeDesc, validationError))
            {
                if (validationError != m_LastValidationError)
                {
                    VULTRA_CORE_ERROR("[DeclarativeRenderer] Invalid render graph '{}': {}", m_Uri, validationError);
                    m_LastValidationError = validationError;
                }
                return;
            }
            m_LastValidationError.clear();

            importDeclarativeGpuSceneBuffers(ctx);

            vrendergraph::RenderGraph graph {
                m_Registry,
                [&ctx](FrameGraph&            fg,
                       const std::string_view resourceName,
                       const nlohmann::json&  resourceDesc) -> FrameGraphResource {
                    if (isBackbufferResource(resourceName))
                        return importRenderGraphBackbuffer(
                            fg, ctx.view(), resourceName, resourceDesc, "VRenderGraphBackbuffer");
                    return ctx.data.tryGet(resourceKeyFor(resourceName));
                }};

            // Resolve $value predicates (feature/platform/backend/XR tokens) from device + view.
            const RenderView& view     = ctx.view();
            const auto         resolver = [&view, &caps](std::string_view key) -> nlohmann::json {
                return renderGraphConditionTokenMatches(key, view, caps);
            };

            m_Owner.m_CurrentBuildContext = &ctx;
            graph.build(ctx.fg, ctx.bb, activeDesc, resolver);
            m_Owner.m_CurrentBuildContext = nullptr;
        }

    private:
        // Remove passes that are inactive for this frame and rewire their consumers to pass through.
        // "Inactive" = disabled (the editor's per-pass enable toggle) or whose XR viewMode doesn't apply
        // to the current view (e.g. a stereo-only synthesis pass in a mono view). This is the per-pass
        // bypass toggle, orthogonal to capability/feature branching - that is now expressed with $-logic
        // nodes and resolved (with liveness culling) inside vrendergraph::RenderGraph::build.
        vrendergraph::RenderGraphDesc makeActiveGraphWithPassthrough(const vrendergraph::RenderGraphDesc& desc,
                                                                     const RenderView&                    view) const
        {
            auto       activeDesc   = desc;
            const auto passIsActive = [&view](const vrendergraph::PassDecl& pass) {
                return pass.enabled && renderGraphViewModeMatches(pass.viewMode, view);
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
                        else if (ports.outputs.size() == 1 && !ports.inputs.empty() &&
                                 (normalizeRenderGraphId(outputSlot) == "color" ||
                                  normalizeRenderGraphId(outputSlot) == "reflection"))
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

        // Resources[] names that at least one pass writes (explicit output mapping). These are
        // "graph-produced" (vs engine-imported data resources like camera/gpu-scene buffers, which no
        // pass output-maps). With $-logic routing, each such resource has exactly one writer in the
        // full graph (the router, or a single pass) - validateRenderGraphStructure enforces that.
        [[nodiscard]] static std::unordered_set<std::string>
        collectGraphProducedResources(const vrendergraph::RenderGraphDesc& desc)
        {
            std::unordered_set<std::string> declared;
            for (const auto& resource : desc.resources)
                declared.insert(resource.name);

            std::unordered_set<std::string> produced;
            for (const auto& pass : desc.passes)
                for (const auto& [slot, ref] : pass.outputs)
                {
                    static_cast<void>(slot);
                    if (declared.contains(ref))
                        produced.insert(ref);
                }
            return produced;
        }

        // Structural validation over the FULL graph (profile-independent). Because branching is now
        // explicit $-logic wiring, a structurally-complete graph is complete on every capability
        // profile - so this single check replaces the old per-profile cross-capability simulation
        // and the fragile "mutually exclusive when / exactly one active writer" rule.
        bool validateRenderGraphStructure(const vrendergraph::RenderGraphDesc& desc, std::string& error) const
        {
            const std::unordered_set<std::string> producedResources = collectGraphProducedResources(desc);
            std::unordered_set<std::string>       resources;
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
                if (!vrendergraph::isLogicNodeType(pass.type) && !m_Registry.contains(pass.type))
                {
                    error = "pass '" + pass.id + "' has unknown type '" + pass.type + "'";
                    return false;
                }
            }

            for (const auto& pass : desc.passes)
            {
                // $-logic nodes are not registry passes; validate their wiring separately.
                if (vrendergraph::isLogicNodeType(pass.type))
                {
                    if (!validateLogicNode(pass, passes, resources, error))
                        return false;
                    continue;
                }

                const auto&                           def = m_Registry.get(pass.type);
                const std::unordered_set<std::string> validInputs(def.inputs.begin(), def.inputs.end());
                const std::unordered_set<std::string> validOutputs(def.outputs.begin(), def.outputs.end());
                const auto                            isOptionalInput = [&](const std::string& slot) {
                    return pass.type == "DirectGBuffer" && slot == "depth";
                };

                for (const auto& slot : def.inputs)
                {
                    const auto it = pass.inputs.find(slot);
                    if (it == pass.inputs.end() || it->second.empty())
                    {
                        if (isOptionalInput(slot))
                            continue;
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

                    // Logic-node sources ($value/$select/...) synthesize their outputs; they are not
                    // registry passes, so skip the registry output-slot check for them.
                    if (vrendergraph::isLogicNodeType(srcPass->second->type))
                        continue;

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

            // --- Declared-resource integrity ---
            // Each declared graph-produced resource must have exactly one writer in the full graph.
            // Mutually-exclusive branches that used to both write a resource now feed a single
            // $select/$switch whose `out` is that resource - so this stays a clean one-writer rule
            // without any profile-specific reasoning.
            std::unordered_map<std::string, int> activeWriters;
            for (const auto& pass : desc.passes)
                for (const auto& [slot, ref] : pass.outputs)
                {
                    static_cast<void>(slot);
                    if (resources.contains(ref))
                        ++activeWriters[ref];
                }

            for (const auto& name : producedResources)
            {
                const auto it    = activeWriters.find(name);
                const int  count = it == activeWriters.end() ? 0 : it->second;
                if (count > 1)
                {
                    error = "declared resource '" + name + "' has " + std::to_string(count) +
                            " writers (expected 1) - funnel mutually-exclusive producers through one $select/$switch";
                    return false;
                }
            }

            // A consumer of a declared graph-produced resource must have a producer in the graph.
            for (const auto& pass : desc.passes)
                for (const auto& [slot, ref] : pass.inputs)
                {
                    const auto parsed = parseRenderGraphResRef(ref);
                    if (!parsed || !producedResources.contains(parsed->node))
                        continue;
                    const auto it = activeWriters.find(parsed->node);
                    if (it == activeWriters.end() || it->second == 0)
                    {
                        error = "pass '" + pass.id + "' input '" + slot + "' reads resource '" + parsed->node +
                                "' which has no producer";
                        return false;
                    }
                }

            // A terminal graph (one that presents to a backbuffer) must have an active backbuffer writer,
            // or nothing is presented (black frame). Feature sub-graphs that never reference a backbuffer
            // are composed by a parent and are exempt.
            const bool expectsBackbuffer =
                std::any_of(desc.resources.begin(), desc.resources.end(), [](const auto& r) {
                    return isBackbufferResource(r.name);
                });
            if (expectsBackbuffer)
            {
                bool backbufferWritten = false;
                for (const auto& pass : desc.passes)
                    for (const auto& [slot, ref] : pass.outputs)
                    {
                        static_cast<void>(slot);
                        if (isBackbufferResource(ref))
                            backbufferWritten = true;
                    }
                if (!backbufferWritten)
                {
                    error = "no active pass writes the backbuffer";
                    return false;
                }
            }

            return true;
        }

        // Validate one $-logic node's wiring (called from validateRenderGraphStructure). Ensures the
        // selector/branch/operand inputs reference existing producers, so a router can always resolve.
        bool validateLogicNode(const vrendergraph::PassDecl&                                          pass,
                               const std::unordered_map<std::string, const vrendergraph::PassDecl*>& passes,
                               const std::unordered_set<std::string>&                                resources,
                               std::string&                                                          error) const
        {
            const auto refExists = [&](const vrendergraph::ResourceRef& ref) {
                if (ref.resource.empty())
                    return false;
                const auto parsed = parseRenderGraphResRef(ref.resource);
                if (!parsed)
                    return false;
                return passes.contains(parsed->node) || resources.contains(parsed->node);
            };
            const auto requireInput = [&](const char* slot) -> bool {
                const auto it = pass.inputs.find(slot);
                if (it == pass.inputs.end() || it->second.empty() || !refExists(it->second))
                {
                    error = std::string("logic node '") + pass.id + "' input '" + slot + "' is not connected";
                    return false;
                }
                return true;
            };

            switch (vrendergraph::logicNodeKind(pass.type))
            {
            case vrendergraph::LogicNodeKind::eValue:
                if (pass.params.get<std::string>("key", std::string {}).empty())
                {
                    error = "value node '" + pass.id + "' has an empty 'key'";
                    return false;
                }
                return true;
            case vrendergraph::LogicNodeKind::eNot:
                return requireInput("a");
            case vrendergraph::LogicNodeKind::eAnd:
            case vrendergraph::LogicNodeKind::eOr:
                if (pass.inputs.empty())
                {
                    error = "logic node '" + pass.id + "' has no operands";
                    return false;
                }
                for (const auto& [slot, ref] : pass.inputs)
                    if (ref.empty() || !refExists(ref))
                    {
                        error = std::string("logic node '") + pass.id + "' operand '" + slot + "' is not connected";
                        return false;
                    }
                return true;
            case vrendergraph::LogicNodeKind::eSelect:
                return requireInput("selector") && requireInput("whenTrue") && requireInput("whenFalse");
            case vrendergraph::LogicNodeKind::eSwitch: {
                if (!requireInput("selector"))
                    return false;
                // Every case slot plus the default slot must be wired.
                const auto& raw = pass.params.raw();
                if (raw.contains("cases") && raw.at("cases").is_object())
                    for (auto it = raw.at("cases").begin(); it != raw.at("cases").end(); ++it)
                        if (it.value().is_string() && !requireInput(it.value().get<std::string>().c_str()))
                            return false;
                const auto def = pass.params.get<std::string>("default", std::string {});
                if (!def.empty() && !requireInput(def.c_str()))
                    return false;
                return true;
            }
            default: return true;
            }
        }

        // Drives one scripted pass's Lua setup/execute closures through the
        // FrameGraph for the current frame. Called from the registry setup lambda
        // (synchronously, while passCtx/params are alive).
        void addScriptedPass(FrameGraphBuildContext&         ctx,
                             vrendergraph::PassBuildContext& passCtx,
                             const vrendergraph::ParamBlock& params,
                             const size_t                    index)
        {
            if (index >= m_Owner.m_Asset.scriptedPasses.size())
                return;
            const auto& def = m_Owner.m_Asset.scriptedPasses[index];

            auto* shaderService = m_Owner.getServices() ? m_Owner.getServices()->tryGet<IShaderService>() : nullptr;
            if (!shaderService)
                return;

            auto& pipelines = m_ScriptedPassPipelines[def.type];
            if (!pipelines)
                pipelines = std::make_unique<ScriptedPassPipelines>();

            const ScriptedPassEnv         env {shaderService, &m_Owner.m_Asset.shaderLibraries, def.sourcePath};
            const uint64_t                generation = ++s_ScriptedPassGeneration;
            const sol::protected_function setupFn    = def.setup;
            const sol::protected_function execFn     = def.execute;
            const std::string             passType   = def.type;

            ctx.fg.addCallbackPass<ScriptedPassFrameData>(
                def.type.c_str(),
                [&ctx, &passCtx, &params, env, generation, setupFn, passType](FrameGraph::Builder&   builder,
                                                                              ScriptedPassFrameData& frame) {
                    PASS_SETUP_ZONE;
                    LuaPassBuildContext buildCtx(builder, ctx, passCtx, params, frame, env, generation);
                    const auto          r = setupFn(buildCtx);
                    if (!r.valid())
                    {
                        const sol::error err = r;
                        logScriptedPassError(passType, "setup", err.what());
                    }
                },
                [execFn, passType, pipelinesPtr = pipelines.get(), services = m_Owner.getServices()](
                    const ScriptedPassFrameData& frame, FrameGraphPassResources& resources, void* ctxPtr) {
                    VULTRA_SCOPED_FRAMEGRAPH_EXEC_CONTEXT(rc, ctxPtr);
                    const std::string scopeName = "RenderGraphPass/" + passType;
                    RHI_GPU_ZONE(rc.cb, scopeName.c_str());
                    assertRenderScriptThread();
                    LuaPassExecContext execCtx(rc, resources, services, frame, *pipelinesPtr);
                    const auto         r = execFn(execCtx);
                    if (!r.valid())
                    {
                        const sol::error err = r;
                        logScriptedPassError(passType, "execute", err.what());
                    }
                });
        }

        void registerPasses()
        {
            // Registration-time shader resolver, used to introspect a pass's shader
            // and expose its reflected params on the graph node. Shader libraries
            // are already loaded at this point (init: loadShaderLibraries before
            // buildRuntimeFeatures).
            auto* regShaderService = m_Owner.getServices() ? m_Owner.getServices()->tryGet<IShaderService>() : nullptr;
            const auto resolveLibAtReg = [this,
                                          regShaderService](const std::string& name) -> rhi::ShaderLibraryRuntime* {
                if (!regShaderService)
                    return nullptr;
                if (name == "builtin")
                    return &regShaderService->builtinLibrary();
                if (auto it = m_Owner.m_Asset.shaderLibraries.find(name); it != m_Owner.m_Asset.shaderLibraries.end())
                    return regShaderService->findProjectLibrary(it->second);
                return nullptr;
            };
            const auto reflectShaderParams =
                [&resolveLibAtReg](const std::string&               libraryName,
                                   const std::string&               shaderId,
                                   const vshadersystem::ShaderStage stage) -> std::vector<vrendergraph::ParamDesc> {
                if (shaderId.empty())
                    return {};
                auto* lib = resolveLibAtReg(libraryName);
                if (!lib)
                    return {};
                auto shader = loadScriptedShader(*lib, shaderId, stage);
                if (!shader)
                    return {};
                return paramsFromShaderReflection(shader->materialDesc);
            };

            for (size_t scriptedIndex = 0; scriptedIndex < m_Owner.m_Asset.scriptedPasses.size(); ++scriptedIndex)
            {
                const auto& def = m_Owner.m_Asset.scriptedPasses[scriptedIndex];
                if (def.type.empty() || !def.setup.valid() || !def.execute.valid())
                    continue;
                if (m_Registry.contains(def.type))
                    continue;

                const auto inputs  = def.inputs.empty() ? std::vector<std::string> {"source"} : def.inputs;
                const auto outputs = def.outputs.empty() ? std::vector<std::string> {"color"} : def.outputs;

                // Node params = reflected (from the optional `shader` hint) overlaid
                // with any explicitly Lua-declared `params` (the latter win).
                std::vector<vrendergraph::ParamDesc> passParams;
                if (!def.reflectFragment.empty())
                    passParams =
                        reflectShaderParams(def.reflectLibrary, def.reflectFragment, vshadersystem::ShaderStage::eFrag);
                else if (!def.reflectCompute.empty())
                    passParams =
                        reflectShaderParams(def.reflectLibrary, def.reflectCompute, vshadersystem::ShaderStage::eComp);
                for (const auto& pd : def.params)
                {
                    auto it = std::find_if(
                        passParams.begin(), passParams.end(), [&](const auto& e) { return e.name == pd.name; });
                    if (it != passParams.end())
                        *it = pd;
                    else
                        passParams.push_back(pd);
                }

                m_Registry.registerPass(vrendergraph::PassDefinition {
                    .type = def.type,
                    .setup =
                        [this, scriptedIndex](FrameGraph&,
                                              FrameGraphBlackboard&,
                                              const vrendergraph::ParamBlock& params,
                                              vrendergraph::PassBuildContext& passCtx) {
                            auto* ctx = m_Owner.m_CurrentBuildContext;
                            if (!ctx)
                                return;
                            addScriptedPass(*ctx, passCtx, params, scriptedIndex);
                        },
                    .inputs  = inputs,
                    .outputs = outputs,
                    .params  = std::move(passParams),
                });
            }

            // Builtin passes: each adapter owns its rhi pass object(s), declares its
            // own ports/params (specs()) and contains its build body. Registered after
            // the scripted-pass loop so a scripted/custom pass of the same type wins.
            m_BuiltinPasses = makeBuiltinRenderGraphPasses();
            for (auto& pass : m_BuiltinPasses)
                pass->registerInto(m_Registry, m_Host);
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
                     "left_backbuffer",
                     "right_backbuffer",
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
        std::unordered_map<std::string, std::unique_ptr<ScriptedPassPipelines>> m_ScriptedPassPipelines;
        std::unordered_set<std::string>                                         m_UnsupportedRayTracingPasses;
        std::string                                                             m_LastValidationError;
        bool                                                                    m_PrefersExplicitPerEye {false};

        // Self-registering builtin passes. Each owns the rhi pass object(s) it drives;
        // m_Host bridges their build() bodies to this renderer's live per-frame state.
        BuiltinPassHost                                       m_Host;
        std::vector<std::unique_ptr<IBuiltinRenderGraphPass>> m_BuiltinPasses;
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
                const auto resolveLibrary = [&](const std::string& libraryName) -> rhi::ShaderLibraryRuntime* {
                    if (libraryName == "builtin")
                        return &shaderService->builtinLibrary();
                    if (auto it = m_Asset.shaderLibraries.find(libraryName); it != m_Asset.shaderLibraries.end())
                        return shaderService->findProjectLibrary(it->second);
                    return nullptr;
                };
                const auto vertexLibraryName =
                    pass.shader.vertexLibrary.empty() ? pass.shader.library : pass.shader.vertexLibrary;
                const auto fragmentLibraryName =
                    pass.shader.fragmentLibrary.empty() ? pass.shader.library : pass.shader.fragmentLibrary;
                auto* vertexLibrary   = resolveLibrary(vertexLibraryName);
                auto* fragmentLibrary = resolveLibrary(fragmentLibraryName);

                if (!vertexLibrary || !fragmentLibrary)
                {
                    VULTRA_CORE_ERROR("[DeclarativeRenderer] Shader libraries '{}/{}' are not loaded for pass '{}'",
                                      vertexLibraryName,
                                      fragmentLibraryName,
                                      pass.name);
                    continue;
                }

                auto runtime        = std::make_unique<RuntimeFeature>();
                runtime->fullscreen = std::make_unique<FullscreenPassRuntime>(pass, vertexLibrary, fragmentLibrary);
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

    bool DeclarativeRenderer::prefersExplicitPerEyeStereo() const
    {
        for (const auto& feature : m_RuntimeFeatures)
        {
            if (feature && feature->renderGraph && feature->renderGraph->prefersExplicitPerEye())
                return true;
        }
        return false;
    }

    bool DeclarativeRenderer::updateRenderGraph(std::string_view uri)
    {
        auto* services     = getServices();
        auto* assetService = services ? services->tryGet<IAssetService>() : nullptr;
        if (!assetService || uri.empty())
            return false;

        auto text = assetService->loadTextAssetSync(uri);
        if (!text)
        {
            VULTRA_CORE_ERROR(
                "[DeclarativeRenderer] Failed to update render graph '{}': {}", uri, std::move(text).error());
            return false;
        }

        vrendergraph::RenderGraphDesc desc;
        try
        {
            const auto json = nlohmann::json::parse(text.value());
            desc            = vrendergraph::loadRenderGraph(json);
        }
        catch (const std::exception& e)
        {
            VULTRA_CORE_ERROR("[DeclarativeRenderer] Failed to parse render graph update '{}': {}", uri, e.what());
            return false;
        }

        bool updated = false;
        for (auto& feature : m_RuntimeFeatures)
        {
            if (!feature || !feature->renderGraph || feature->renderGraph->uri() != uri)
                continue;
            feature->renderGraph->updateDesc(desc);
            updated = true;
        }
        if (!updated)
        {
            VULTRA_CORE_WARN("[DeclarativeRenderer] Render graph '{}' is not active in renderer '{}'", uri, name());
            return false;
        }
        return true;
    }

    void DeclarativeRenderer::invalidateShaderPipelines()
    {
        for (auto& feature : m_RuntimeFeatures)
        {
            if (!feature)
                continue;
            if (feature->fullscreen)
                feature->fullscreen->invalidatePipelines();
            if (feature->renderGraph)
                feature->renderGraph->invalidateShaderPipelines();
        }
    }

    // Kept here (not in declarative_renderer_asset_loader.cpp) on purpose: the render
    // script state is runtime infrastructure -- it registers the scripted-pass bindings
    // and records the owning thread for the exec-time assertion, both anon-namespace
    // state of this TU.
    sol::state& DeclarativeRenderer::renderScriptState()
    {
        if (!m_RenderScriptState)
        {
            m_RenderScriptState = std::make_unique<sol::state>();
            auto& lua           = *m_RenderScriptState;
            lua.open_libraries(sol::lib::base, sol::lib::table, sol::lib::string, sol::lib::math);
            lua.set_function("RenderPipelineAsset", [](sol::table t) { return t; });
            lua.set_function("RenderFeature", [](sol::table t) { return t; });
            lua.set_function("RenderGraphPass", [](sol::table t) { return t; });
            lua.set_function("ShaderLibrary", [](sol::table t) { return t; });
            registerScriptedPassLuaBindings(lua);
            // Option A invariant: scripted execute runs on the thread that built the
            // state (the render-script thread). Recorded for the exec-time assertion.
            s_RenderScriptThreadId = std::this_thread::get_id();
        }
        return *m_RenderScriptState;
    }

    bool DeclarativeRenderer::parseScriptedPassTable(sol::table table, ScriptedPassDef& outPass)
    {
        outPass.type = getString(table, "type");
        if (outPass.type.empty())
            outPass.type = getString(table, "name");
        if (outPass.type.empty())
            return false;

        sol::object setupObj = table["setup"];
        sol::object execObj  = table["execute"];
        if (setupObj.get_type() != sol::type::function || execObj.get_type() != sol::type::function)
            return false;

        outPass.setup   = setupObj.as<sol::protected_function>();
        outPass.execute = execObj.as<sol::protected_function>();
        outPass.inputs  = getStringList(table, "inputs");
        outPass.outputs = getStringList(table, "outputs");
        if (outPass.inputs.empty())
            outPass.inputs.push_back(getString(table, "input", "source"));
        if (outPass.outputs.empty())
            outPass.outputs.push_back(getString(table, "output", "color"));
        outPass.params = parseScriptedPassParams(table);

        // Optional `shader` hint: used only to auto-expose the shader's reflected
        // params on the graph node (the real shader is still chosen in `setup`).
        sol::object shaderObj = table["shader"];
        if (shaderObj.is<sol::table>())
        {
            sol::table st           = shaderObj.as<sol::table>();
            outPass.reflectLibrary  = getString(st, "fragmentLibrary", getString(st, "library", "project"));
            outPass.reflectFragment = getString(st, "fragment");
            outPass.reflectCompute  = getString(st, "compute");
        }
        return true;
    }

    // Authoring-facing vocabulary for $value nodes. MUST stay in sync with the runtime evaluator
    // renderGraphConditionTokenMatches() above (which maps each key to a device/view capability).
    std::span<const std::string_view> renderGraphValueKeys()
    {
        static constexpr std::string_view kKeys[] = {
            "feature_bindless",
            "platform_desktop",
            "platform_web",
            "platform_android",
            "backend_vulkan",
            "backend_webgpu",
            "tier_highend",
            "tier_compat",
            "feature_raytracing",
            "feature_rayquery",
            "feature_raytracing_pipeline",
            "feature_meshshader",
            "feature_xr",
            "xr",
            "mono",
            "xr_eye_targets",
            "always",
            "never",
        };
        return kKeys;
    }

} // namespace vultra
