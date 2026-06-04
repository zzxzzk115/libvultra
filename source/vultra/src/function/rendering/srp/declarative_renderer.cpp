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
#include "vultra/function/rendering/srp/builtin/passes/ui_overlay_pass.hpp"
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
#include <cstddef>
#include <cstdint>
#include <cstring>
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

        [[nodiscard]] int getInt(sol::table table, const char* key, int fallback = 0)
        {
            sol::object value = table[key];
            return value.is<int>() ? value.as<int>() : fallback;
        }

        [[nodiscard]] bool getBool(sol::table table, const char* key, bool fallback = false)
        {
            sol::object value = table[key];
            return value.is<bool>() ? value.as<bool>() : fallback;
        }

        template<typename T>
        [[nodiscard]] T shaderParamDefaultValue(const vshadersystem::ParamDefault& value)
        {
            T out {};
            std::memcpy(&out, value.valueBuffer, std::min(sizeof(T), sizeof(value.valueBuffer)));
            return out;
        }

        template<typename T>
        void writePushConstantValue(std::vector<std::byte>& bytes,
                                    const vshadersystem::MaterialParamDesc& param,
                                    const T& value)
        {
            if (param.offset >= bytes.size())
                return;

            const auto size = std::min<size_t>({sizeof(T), param.size, bytes.size() - param.offset});
            std::memcpy(bytes.data() + param.offset, &value, size);
        }

        [[nodiscard]] std::vector<std::byte>
        packShaderParams(const vshadersystem::MaterialDescription& materialDesc, const vrendergraph::ParamBlock& params)
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
                        const auto value = static_cast<uint32_t>(std::max(params.get<int>(param.name,
                                                                                          static_cast<int>(fallback)),
                                                                          0));
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

        [[nodiscard]] std::vector<std::string> getStringList(sol::table table, const char* key)
        {
            std::vector<std::string> out;
            sol::object              value = table[key];
            if (value.is<std::string>())
                out.push_back(value.as<std::string>());
            else if (value.is<sol::table>())
            {
                sol::table values = value.as<sol::table>();
                for (const auto& [_, item] : values)
                {
                    static_cast<void>(_);
                    if (item.is<std::string>())
                        out.push_back(item.as<std::string>());
                }
            }

            out.erase(std::remove_if(out.begin(), out.end(), [](const auto& item) { return item.empty(); }), out.end());
            out.erase(std::unique(out.begin(), out.end()), out.end());
            return out;
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

        [[nodiscard]] std::string uriFromLogicalPath(std::string_view logicalPath)
        {
            return logicalPath.starts_with("res://") ? std::string(logicalPath) : "res://" + std::string(logicalPath);
        }

        [[nodiscard]] bool isImportedAssetPath(std::string_view logicalPath)
        {
            return logicalPath == "imported" || logicalPath.starts_with("imported/");
        }

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

        enum class RenderGraphBackbufferView : uint8_t
        {
            eCurrent,
            eLeft,
            eRight,
        };

        [[nodiscard]] std::optional<std::string> selectorString(const nlohmann::json& selector, const char* key)
        {
            if (!selector.is_object())
                return std::nullopt;
            const auto it = selector.find(key);
            if (it != selector.end() && it->is_string())
                return it->get<std::string>();
            const auto descIt = selector.find("desc");
            if (descIt == selector.end() || !descIt->is_object())
                return std::nullopt;
            const auto nestedIt = descIt->find(key);
            if (nestedIt == descIt->end() || !nestedIt->is_string())
                return std::nullopt;
            return nestedIt->get<std::string>();
        }

        [[nodiscard]] RenderGraphBackbufferView backbufferViewFromResource(std::string_view name)
        {
            const auto normalized = normalizeId(std::string(name));
            if (normalized == "left_backbuffer" || normalized == "backbuffer_left" || normalized == "left_target" ||
                normalized == "target_left")
                return RenderGraphBackbufferView::eLeft;
            if (normalized == "right_backbuffer" || normalized == "backbuffer_right" || normalized == "right_target" ||
                normalized == "target_right")
                return RenderGraphBackbufferView::eRight;
            return RenderGraphBackbufferView::eCurrent;
        }

        [[nodiscard]] RenderGraphBackbufferView backbufferViewFromSelector(const nlohmann::json&           selector,
                                                                           const RenderGraphBackbufferView fallback)
        {
            const auto view = selectorString(selector, "view");
            if (!view)
                return fallback;

            const auto normalized = normalizeId(*view);
            if (normalized == "left" || normalized == "eye0" || normalized == "eye_0")
                return RenderGraphBackbufferView::eLeft;
            if (normalized == "right" || normalized == "eye1" || normalized == "eye_1")
                return RenderGraphBackbufferView::eRight;
            return fallback;
        }

        [[nodiscard]] bool isBackbufferResource(std::string_view name)
        {
            const auto normalized = normalizeId(std::string(name));
            return normalized == "backbuffer" || normalized == "target" || normalized == "left_backbuffer" ||
                   normalized == "backbuffer_left" || normalized == "left_target" || normalized == "target_left" ||
                   normalized == "right_backbuffer" || normalized == "backbuffer_right" ||
                   normalized == "right_target" || normalized == "target_right";
        }

        void warnMissingBackbufferOnce(std::string_view resourceName, const RenderGraphBackbufferView view)
        {
            static std::array<bool, 3> warned {};
            const auto                 index = static_cast<size_t>(view);
            if (index < warned.size() && warned[index])
                return;
            if (index < warned.size())
                warned[index] = true;

            const char* label = "current";
            if (view == RenderGraphBackbufferView::eLeft)
                label = "left";
            else if (view == RenderGraphBackbufferView::eRight)
                label = "right";

            VULTRA_CORE_ERROR("[DeclarativeRenderer] Render graph requested {} backbuffer '{}' but no target is "
                              "available for the current view.",
                              label,
                              resourceName);
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

        [[nodiscard]] FrameGraphResource importRenderGraphBackbuffer(FrameGraph&            fg,
                                                                     const RenderView&      view,
                                                                     std::string_view       resourceName,
                                                                     const nlohmann::json&  selector,
                                                                     const std::string_view importName)
        {
            const auto requestedView = backbufferViewFromSelector(selector, backbufferViewFromResource(resourceName));

            rhi::Texture* target   = view.target;
            uint32_t      viewMask = view.renderTargetViewMask();
            if (requestedView == RenderGraphBackbufferView::eLeft || requestedView == RenderGraphBackbufferView::eRight)
            {
                const auto eyeIndex = requestedView == RenderGraphBackbufferView::eLeft ? 0u : 1u;
                target              = view.xrEyeTargets[eyeIndex];
                viewMask            = 0u;
            }

            if (!target)
            {
                warnMissingBackbufferOnce(resourceName, requestedView);
                return {};
            }

            std::string name {importName};
            if (requestedView == RenderGraphBackbufferView::eLeft)
                name += "/Left";
            else if (requestedView == RenderGraphBackbufferView::eRight)
                name += "/Right";
            return framegraph::importTexture(fg, name, target, viewMask);
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

        [[nodiscard]] bool renderGraphViewModeMatches(std::string_view viewMode, const RenderView& view)
        {
            const auto normalized = normalizeId(std::string(viewMode));
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

        void importDeclarativeGpuSceneBuffers(FrameGraphBuildContext& ctx)
        {
            auto* gpuSceneDatabase = ctx.view().gpuSceneDatabase;
            if (!gpuSceneDatabase || !gpuSceneDatabase->resources)
                return;

            const auto importStorage = [&ctx](const FrameGraphResourceKey key,
                                              const char*                 name,
                                              rhi::Buffer*                buffer) {
                if (!buffer || !(*buffer) || ctx.data.contains(key))
                    return;
                ctx.data.set(key, framegraph::importBuffer(ctx.fg, name, buffer, framegraph::BufferType::eStorageBuffer));
            };
            importStorage(kResKey_InstanceBuffer, "ImportedInstanceBuffer", gpuSceneDatabase->instanceBuffer.get());
            importStorage(kResKey_MeshTableBuffer, "ImportedMeshTableBuffer", gpuSceneDatabase->meshTableBuffer.get());
            importStorage(kResKey_TransformBuffer, "ImportedTransformBuffer", gpuSceneDatabase->transformBuffer.get());
            importStorage(kResKey_SkinMatrixBuffer, "ImportedSkinMatrixBuffer", gpuSceneDatabase->skinMatrixBuffer.get());
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

    void registerBuiltinRenderGraphPasses(vrendergraph::RenderGraphRegistry& registry)
    {
        const auto noop =
            [](FrameGraph&, FrameGraphBlackboard&, const vrendergraph::ParamBlock&, vrendergraph::PassBuildContext&) {};
        const auto pass = [&](std::string                          type,
                              std::vector<std::string>             inputs,
                              std::vector<std::string>             outputs,
                              std::vector<vrendergraph::ParamDesc> params = {}) {
            if (registry.contains(type))
                return;
            registry.registerPass(vrendergraph::PassDefinition {
                .type    = std::move(type),
                .setup   = noop,
                .inputs  = std::move(inputs),
                .outputs = std::move(outputs),
                .params  = std::move(params),
            });
        };

        pass("CameraClear", {}, {"color"});
        pass("CompatibilityBaseColor", {}, {"color"});
        pass("DirectGBuffer", {"depth"}, {"color", "depth", "normal", "material", "entityId"});
        pass("DirectDepthPre", {}, {"depth"});
        pass("DepthPre", {}, {"depth"});
        pass("ShadowMap",
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
                 {.name = "stableTexelSnapping", .type = vrendergraph::ParamType::eBoolean, .defaultValue = true},
                 {.name = "depthBias", .type = vrendergraph::ParamType::eFloat, .defaultValue = 0.0012f},
                 {.name = "normalBias", .type = vrendergraph::ParamType::eFloat, .defaultValue = 0.015f},
                 {.name = "pcssLightRadius", .type = vrendergraph::ParamType::eFloat, .defaultValue = 1.5f},
             });
        pass("DeferredLighting", {"color", "normal", "material", "depth", "ao", "shadowMap", "shadowData"}, {"color"});
        pass("HzbGenerate", {"depth"}, {"hzb"});
        pass("Ssao",
             {"depth", "normal"},
             {"ao"},
             {
                 {.name = "enabled", .type = vrendergraph::ParamType::eBoolean, .defaultValue = false},
                 {.name = "maxRadiusPixels", .type = vrendergraph::ParamType::eInt, .defaultValue = 16},
                 {.name = "stepCount", .type = vrendergraph::ParamType::eInt, .defaultValue = 2},
                 {.name = "directionCount", .type = vrendergraph::ParamType::eInt, .defaultValue = 4},
             });
        pass("Ssr",
             {"color", "depth", "normal", "material"},
             {"reflection"},
             {
                 {.name = "enabled", .type = vrendergraph::ParamType::eBoolean, .defaultValue = false},
                 {.name = "maxSteps", .type = vrendergraph::ParamType::eInt, .defaultValue = 8},
                 {.name = "binaryRefinement", .type = vrendergraph::ParamType::eInt, .defaultValue = 2},
             });
        pass("SsrComposite",
             {"source", "reflection"},
             {"color"},
             {{.name = "enabled", .type = vrendergraph::ParamType::eBoolean, .defaultValue = true}});
        pass("ToneMapping",
             {"source"},
             {"color"},
             {{.name = "enabled", .type = vrendergraph::ParamType::eBoolean, .defaultValue = true}});
        pass("Fxaa",
             {"source"},
             {"color"},
             {{.name = "enabled", .type = vrendergraph::ParamType::eBoolean, .defaultValue = true}});
        pass("SelectionOutline",
             {"source", "entityId", "depth"},
             {"color"},
             {{.name = "enabled", .type = vrendergraph::ParamType::eBoolean, .defaultValue = true}});
        pass("UiOverlay",
             {"source"},
             {"color"},
             {{.name = "enabled", .type = vrendergraph::ParamType::eBoolean, .defaultValue = true}});
        pass("XrGeometryWarp",
             {"source", "depth"},
             {"color"},
             {
                 {.name = "enabled", .type = vrendergraph::ParamType::eBoolean, .defaultValue = true},
                 {.name = "sourceView", .type = vrendergraph::ParamType::eString, .defaultValue = "left"},
                 {.name = "targetView", .type = vrendergraph::ParamType::eString, .defaultValue = "right"},
                 {.name = "gridSize", .type = vrendergraph::ParamType::eInt, .defaultValue = 4},
                 {.name = "warpStrength", .type = vrendergraph::ParamType::eFloat, .defaultValue = 0.035f},
             });
        pass("XrPullPushInpaint",
             {"source"},
             {"color"},
             {{.name = "enabled", .type = vrendergraph::ParamType::eBoolean, .defaultValue = true}});
        pass("FinalComposition", {"source"}, {"target"});
        pass("RayTracingPrimary", {}, {"color"});
        pass("VisibilityBuffer", {}, {"visibility", "depth"});
        pass("ThinGBuffer", {"visibility", "depth"}, {"color", "normal", "material", "depth", "entityId"});
        pass("CoarseInstanceCull", {}, {"visibleInstance", "visibleInstanceCount", "meshletCullDispatchArgs"});
        pass("MeshletCull",
             {"visibleInstance", "visibleInstanceCount", "meshletCullDispatchArgs"},
             {"visibleMeshlet", "visibleMeshletCount"});
        pass("BuildIndirect",
             {"visibleMeshlet", "visibleMeshletCount"},
             {"draw",
              "instance",
              "meshTable",
              "transform",
              "meshlets",
              "visibleMeshlet",
              "visibleMeshletCount",
              "materialTable"});
        pass("DrawsetBuild", {"draw", "meshlets"}, {"draw", "meshlets", "indirect", "drawSet"});
        pass("MeshletHiZCull", {}, {"visibleMeshlet", "visibleMeshletCount"});
        pass("GeneralGaussianSplatPreprocess",
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
              "sh"});
        pass("GeneralGaussianSplatRender", {}, {"color"});
        pass("GeneralGaussianSplatComposite", {"source"}, {"color"});
        pass("GeneralGaussianSplatFoveatedComposite", {"fovea", "mid", "outer", "base"}, {"color"});
    }

    class DeclarativeRenderer::FullscreenPassRuntime
    {
    public:
        explicit FullscreenPassRuntime(FullscreenPass      desc,
                                       rhi::ShaderLibraryRuntime* vertexShaderLibrary,
                                       rhi::ShaderLibraryRuntime* fragmentShaderLibrary) :
            m_Desc(std::move(desc)),
            m_VertexShaderLibrary(vertexShaderLibrary),
            m_FragmentShaderLibrary(fragmentShaderLibrary)
        {}

        void update(FullscreenPass      desc,
                    rhi::ShaderLibraryRuntime* vertexShaderLibrary,
                    rhi::ShaderLibraryRuntime* fragmentShaderLibrary)
        {
            const bool pipelineKeyChanged = vertexShaderLibrary != m_VertexShaderLibrary ||
                                            fragmentShaderLibrary != m_FragmentShaderLibrary ||
                                            desc.shader.library != m_Desc.shader.library ||
                                            desc.shader.vertexLibrary != m_Desc.shader.vertexLibrary ||
                                            desc.shader.fragmentLibrary != m_Desc.shader.fragmentLibrary ||
                                            desc.shader.vertex != m_Desc.shader.vertex ||
                                            desc.shader.fragment != m_Desc.shader.fragment;
            m_Desc                  = std::move(desc);
            m_VertexShaderLibrary   = vertexShaderLibrary;
            m_FragmentShaderLibrary = fragmentShaderLibrary;
            if (pipelineKeyChanged)
                m_Pipelines.clear();
        }

        void invalidatePipelines() { m_Pipelines.clear(); }

        FrameGraphResource addPass(FrameGraphBuildContext& ctx,
                                   FrameGraphResource      directInput        = {},
                                   FrameGraphResource      directOutput       = {},
                                   const bool              publishNamedOutput = true,
                                   const vrendergraph::ParamBlock& params = {})
        {
            if (!ctx.view().target || !m_VertexShaderLibrary || !m_FragmentShaderLibrary)
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

        std::optional<rhi::ShaderLibraryRuntime::LoadedShader> loadShader(rhi::ShaderLibraryRuntime&       shaderLibrary,
                                                                          const std::string&               shaderId,
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
            auto shader = loadShader(*m_FragmentShaderLibrary, m_Desc.shader.fragment, vshadersystem::ShaderStage::eFrag);
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

    class DeclarativeRenderer::ComputePassRuntime
    {
    public:
        explicit ComputePassRuntime(ProjectGraphPass desc, rhi::ShaderLibraryRuntime* shaderLibrary) :
            m_Desc(std::move(desc)), m_ShaderLibrary(shaderLibrary)
        {}

        void update(ProjectGraphPass desc, rhi::ShaderLibraryRuntime* shaderLibrary)
        {
            const bool pipelineKeyChanged = shaderLibrary != m_ShaderLibrary ||
                                            desc.shader.library != m_Desc.shader.library ||
                                            desc.shader.compute != m_Desc.shader.compute;
            m_Desc          = std::move(desc);
            m_ShaderLibrary = shaderLibrary;
            if (pipelineKeyChanged)
                m_Pipeline.reset();
        }

        void invalidatePipelines() { m_Pipeline.reset(); }

        FrameGraphResource addPass(FrameGraphBuildContext&             ctx,
                                   std::vector<FrameGraphResource>        inputs,
                                   const std::string_view              outputSlot,
                                   const vrendergraph::ParamBlock&     params)
        {
            if (!ctx.view().target || !m_ShaderLibrary || m_Desc.shader.compute.empty())
                return {};

            struct PassData
            {
                std::vector<FrameGraphResource> inputs;
                FrameGraphResource              output;
            };

            auto outputDesc = makeOutputDesc(ctx, inputs);
            auto outputName = std::string {outputSlot};
            auto pushConstants = makePushConstants(params);
            FrameGraphResource output {};

            ctx.fg.addCallbackPass<PassData>(
                m_Desc.type.c_str(),
                [this, inputs = std::move(inputs), outputDesc, outputName = std::move(outputName), &output](
                    FrameGraph::Builder& builder, PassData& data) mutable {
                    PASS_SETUP_ZONE;

                    data.inputs.reserve(inputs.size());
                    for (uint32_t i = 0; i < inputs.size(); ++i)
                    {
                        if (!inputs[i])
                            continue;

                        data.inputs.push_back(
                            builder.read(inputs[i],
                                         framegraph::TextureRead {
                                             .binding =
                                                 {
                                                     .location      = {.set = 3, .binding = i},
                                                     .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                                 },
                                             .type        = framegraph::TextureRead::Type::eSampledImage,
                                             .imageAspect = rhi::ImageAspect::eColor,
                                         }));
                    }

                    output = builder.create<framegraph::FrameGraphTexture>(
                        std::string {m_Desc.type} + " " + outputName, outputDesc);
                    data.output = builder.write(output,
                                                framegraph::ImageWrite {
                                                    .binding =
                                                        {
                                                            .location = {.set = 3,
                                                                         .binding = static_cast<uint32_t>(inputs.size())},
                                                            .pipelineStage = framegraph::PipelineStage::eComputeShader,
                                                        },
                                                    .imageAspect = rhi::ImageAspect::eColor,
                                                });
                },
                [this, outputDesc, pushConstants = std::move(pushConstants)](
                    const PassData&, FrameGraphPassResources&, void* ctxPtr) {
                    VULTRA_SCOPED_FRAMEGRAPH_EXEC_CONTEXT(rc, ctxPtr);
                    if (!m_ShaderLibrary)
                        return;

                    auto* pipeline = getPipeline(rc.rd);
                    if (!pipeline)
                        return;

                    RHI_GPU_ZONE(rc.cb, m_Desc.type.c_str());
                    rc.cb.bindPipeline(*pipeline);
                    rc.bindDescriptorSets(*pipeline);
                    if (!pushConstants.empty())
                    {
                        rc.cb.pushConstants(rhi::ShaderStages::eCompute,
                                            0,
                                            static_cast<uint32_t>(pushConstants.size()),
                                            pushConstants.data());
                    }
                    rc.cb.dispatch(resolveDispatchSize(*pipeline, outputDesc.extent));
                });

            return output;
        }

    private:
        framegraph::FrameGraphTexture::Desc makeOutputDesc(
            FrameGraphBuildContext&                ctx,
            const std::vector<FrameGraphResource>& inputs) const
        {
            constexpr auto usage =
                rhi::ImageUsage::eStorage | rhi::ImageUsage::eSampled | rhi::ImageUsage::eTransferSrc;
            auto desc = makeRenderViewTextureDesc(ctx.view(), rhi::PixelFormat::eRGBA16F, usage);

            for (const auto input : inputs)
            {
                if (!input)
                    continue;

                const auto inputDesc = ctx.fg.getDescriptor<framegraph::FrameGraphTexture>(input);
                desc                 = makeInheritedTextureDesc(inputDesc, rhi::PixelFormat::eRGBA16F, usage);
                break;
            }

            return desc;
        }

        glm::uvec3 resolveDispatchSize(const rhi::ComputePipeline& pipeline, const rhi::Extent2D extent) const
        {
            glm::uvec3 dispatch {
                std::max(m_Desc.dispatchX, 1u),
                std::max(m_Desc.dispatchY, 1u),
                std::max(m_Desc.dispatchZ, 1u),
            };

            if (!m_Desc.dispatchByOutputSize)
                return dispatch;

            const auto localSize = pipeline.getWorkGroupSize();
            const auto localX    = std::max(localSize.x, 1u);
            const auto localY    = std::max(localSize.y, 1u);
            dispatch.x           = (extent.width + localX - 1u) / localX;
            dispatch.y           = (extent.height + localY - 1u) / localY;
            return dispatch;
        }

        rhi::ComputePipeline* getPipeline(rhi::RenderDevice& rd)
        {
            if (m_Pipeline)
                return &m_Pipeline.value();

            auto shader = loadShader(m_Desc.shader.compute);
            if (!shader)
                return nullptr;

            m_Pipeline = rd.createComputePipelineBuiltin(*shader);
            return &m_Pipeline.value();
        }

        std::optional<rhi::ShaderLibraryRuntime::LoadedShader> loadShader(const std::string& shaderId) const
        {
            if (!m_ShaderLibrary)
                return std::nullopt;

            std::vector<std::string> shaderIds {shaderId};
            if (shaderId.find('/') == std::string::npos && shaderId.find('\\') == std::string::npos)
                shaderIds.push_back("compute/" + shaderId);

            for (const auto& id : shaderIds)
            {
                const auto hash =
                    rhi::ShaderLibraryRuntime::computeVariantHash(id, vshadersystem::ShaderStage::eComp, {});
                if (!m_ShaderLibrary->hasVariant(hash, vshadersystem::ShaderStage::eComp))
                    continue;
                auto shader = m_ShaderLibrary->load(hash, vshadersystem::ShaderStage::eComp);
                if (shader)
                    return shader;
            }

            VULTRA_CORE_ERROR("[DeclarativeRenderer] Failed to load compute shader '{}' for graph pass '{}'",
                              shaderId,
                              m_Desc.type);
            return std::nullopt;
        }

        std::vector<std::byte> makePushConstants(const vrendergraph::ParamBlock& params) const
        {
            auto shader = loadShader(m_Desc.shader.compute);
            if (!shader)
                return {};
            return packShaderParams(shader->materialDesc, params);
        }

    private:
        ProjectGraphPass                       m_Desc;
        rhi::ShaderLibraryRuntime*             m_ShaderLibrary {nullptr};
        std::optional<rhi::ComputePipeline>    m_Pipeline;
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

        [[nodiscard]] std::string_view uri() const { return m_Uri; }

        void updateDesc(vrendergraph::RenderGraphDesc desc)
        {
            m_Desc = std::move(desc);
            m_LastValidationError.clear();
        }

        void invalidateShaderPipelines()
        {
            for (auto& [_, runtime] : m_ProjectPassRuntimes)
            {
                if (runtime)
                    runtime->invalidatePipelines();
            }
            for (auto& [_, runtime] : m_ProjectComputePassRuntimes)
            {
                if (runtime)
                    runtime->invalidatePipelines();
            }
        }

        void build(FrameGraphBuildContext& ctx)
        {
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
                return pass.enabled && renderGraphViewModeMatches(pass.viewMode, view) &&
                       renderGraphConditionMatches(pass.when, view);
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
                                 (normalizeId(outputSlot) == "color" || normalizeId(outputSlot) == "reflection"))
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
                const auto isOptionalInput = [&](const std::string& slot) {
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
            for (size_t projectPassIndex = 0; projectPassIndex < m_Owner.m_Asset.projectGraphPasses.size();
                 ++projectPassIndex)
            {
                const auto& projectPass = m_Owner.m_Asset.projectGraphPasses[projectPassIndex];
                if (projectPass.type.empty())
                    continue;

                const auto inputs =
                    projectPass.inputs.empty() ? std::vector<std::string> {"source"} : projectPass.inputs;
                const auto outputs =
                    projectPass.outputs.empty() ? std::vector<std::string> {"color"} : projectPass.outputs;

                if (projectPass.pipeline == ProjectGraphPass::Pipeline::eGraphics &&
                    projectPass.fullscreen.shader.fragment.empty())
                    continue;
                if (projectPass.pipeline == ProjectGraphPass::Pipeline::eCompute && projectPass.shader.compute.empty())
                    continue;

                m_Registry.registerPass(vrendergraph::PassDefinition {
                    .type = projectPass.type,
                    .setup =
                        [this, projectPassIndex](FrameGraph&,
                                                 FrameGraphBlackboard&,
                                                 const vrendergraph::ParamBlock& params,
                                                 vrendergraph::PassBuildContext& passCtx) {
                            auto* ctx = m_Owner.m_CurrentBuildContext;
                            if (!ctx)
                                return;
                            if (projectPassIndex >= m_Owner.m_Asset.projectGraphPasses.size())
                                return;

                            static const std::string kDefaultInputSlot {"source"};
                            static const std::string kDefaultOutputSlot {"color"};
                            const auto&              projectPass = m_Owner.m_Asset.projectGraphPasses[projectPassIndex];
                            const auto inputCount = projectPass.inputs.empty() ? size_t {1} : projectPass.inputs.size();
                            const auto outputCount =
                                projectPass.outputs.empty() ? size_t {1} : projectPass.outputs.size();
                            const auto inputSlot = [&](const size_t index) -> const std::string& {
                                return projectPass.inputs.empty() ? kDefaultInputSlot : projectPass.inputs[index];
                            };
                            const auto outputSlot = [&](const size_t index) -> const std::string& {
                                return projectPass.outputs.empty() ? kDefaultOutputSlot : projectPass.outputs[index];
                            };
                            if (inputCount == 0u || outputCount == 0u)
                                return;

                            auto* shaderService =
                                m_Owner.getServices() ? m_Owner.getServices()->tryGet<IShaderService>() : nullptr;
                            if (!shaderService)
                                return;

                            const auto resolveLibrary =
                                [&](const std::string& libraryName) -> rhi::ShaderLibraryRuntime* {
                                if (libraryName == "builtin")
                                    return &shaderService->builtinLibrary();
                                if (auto it = m_Owner.m_Asset.shaderLibraries.find(libraryName);
                                    it != m_Owner.m_Asset.shaderLibraries.end())
                                    return shaderService->findProjectLibrary(it->second);
                                return nullptr;
                            };

                            if (projectPass.pipeline == ProjectGraphPass::Pipeline::eGraphics)
                            {
                                auto pass = projectPass.fullscreen;
                                pass.name =
                                    params.get<std::string>("name", pass.name.empty() ? projectPass.type : pass.name);

                                const auto vertexLibraryName =
                                    pass.shader.vertexLibrary.empty() ? pass.shader.library : pass.shader.vertexLibrary;
                                const auto fragmentLibraryName = pass.shader.fragmentLibrary.empty() ?
                                                                     pass.shader.library :
                                                                     pass.shader.fragmentLibrary;
                                auto* vertexLibrary   = resolveLibrary(vertexLibraryName);
                                auto* fragmentLibrary = resolveLibrary(fragmentLibraryName);
                                if (!vertexLibrary || !fragmentLibrary)
                                {
                                    VULTRA_CORE_ERROR("[DeclarativeRenderer] Shader libraries '{}/{}' are not loaded for "
                                                      "project graph pass '{}'",
                                                      vertexLibraryName,
                                                      fragmentLibraryName,
                                                      projectPass.type);
                                    return;
                                }

                                auto& runtime = m_ProjectPassRuntimes[pass.name];
                                if (!runtime)
                                    runtime = std::make_unique<FullscreenPassRuntime>(
                                        pass, vertexLibrary, fragmentLibrary);
                                else
                                    runtime->update(pass, vertexLibrary, fragmentLibrary);

                                const auto input  = passCtx.getInput(pass.input.empty() ? inputSlot(0) : pass.input);
                                const auto output = runtime->addPass(*ctx, input, {}, false, params);
                                if (output)
                                    passCtx.setOutput(pass.output.empty() ? outputSlot(0) : pass.output, output);
                                return;
                            }

                            if (projectPass.pipeline == ProjectGraphPass::Pipeline::eCompute)
                            {
                                auto pass = projectPass;
                                pass.type = params.get<std::string>("name", pass.type);

                                auto* library = resolveLibrary(pass.shader.library);
                                if (!library)
                                {
                                    VULTRA_CORE_ERROR("[DeclarativeRenderer] Shader library '{}' is not loaded for "
                                                      "project compute pass '{}'",
                                                      pass.shader.library,
                                                      projectPass.type);
                                    return;
                                }

                                auto& runtime = m_ProjectComputePassRuntimes[pass.type];
                                if (!runtime)
                                    runtime = std::make_unique<ComputePassRuntime>(pass, library);
                                else
                                    runtime->update(pass, library);

                                std::vector<FrameGraphResource> inputResources;
                                inputResources.reserve(inputCount);
                                for (size_t i = 0; i < inputCount; ++i)
                                    inputResources.push_back(passCtx.getInput(inputSlot(i)));

                                const auto output = runtime->addPass(
                                    *ctx, std::move(inputResources), outputSlot(0), params);
                                if (output)
                                    passCtx.setOutput(outputSlot(0), output);
                                return;
                            }

                            if (!m_UnsupportedRayTracingPasses.contains(projectPass.type))
                            {
                                VULTRA_CORE_ERROR("[DeclarativeRenderer] Project graph pass '{}' declares a "
                                                  "raytracing pipeline, but script-defined raytracing passes need TLAS/"
                                                  "SBT binding support before execution",
                                                  projectPass.type);
                                m_UnsupportedRayTracingPasses.insert(projectPass.type);
                            }

                            passCtx.setOutput(outputSlot(0), passCtx.getInput(inputSlot(0)));
                        },
                    .inputs  = inputs,
                    .outputs = outputs,
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

            registerBuiltin("CameraClear",
                            {},
                            {"color"},
                            [this](FrameGraph&,
                                   FrameGraphBlackboard&,
                                   const vrendergraph::ParamBlock&,
                                   vrendergraph::PassBuildContext& passCtx) {
                                auto* ctx = m_Owner.m_CurrentBuildContext;
                                if (!ctx || !ctx->view().target)
                                    return;

                                struct PassData
                                {
                                    FrameGraphResource color;
                                };

                                const auto desc =
                                    makeRenderViewTextureDesc(ctx->view(), ctx->view().target->getPixelFormat());
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

                                        auto& attachment = framebufferInfo->colorAttachments[0];
                                        attachment.clearValue =
                                            rc.view().camera ? rc.view().camera->clearValue : rc.view().clearValue;
                                        attachment.loadOp = rhi::AttachmentLoadOp::eClear;
                                        rc.cb.beginRendering(*framebufferInfo).endRendering();
                                    });

                                ctx->data.set(kResKey_FinalCompositionSource, data.color);
                                passCtx.setOutput("color", data.color);
                            });

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
                            {"depth"},
                            {"color", "depth", "normal", "material", "entityId"},
                            [this](FrameGraph&,
                                   FrameGraphBlackboard&,
                                   const vrendergraph::ParamBlock&,
                                   vrendergraph::PassBuildContext& passCtx) {
                                auto* ctx = m_Owner.m_CurrentBuildContext;
                                if (!ctx)
                                    return;
                                auto color = m_DirectGBufferPass.addPass(*ctx, passCtx.getInput("depth"));
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
                                if (auto res = ctx->data.tryGet(kResKey_GBufferEntityId))
                                    passCtx.setOutput("entityId", res);
                                else
                                    passCtx.setOutput("entityId", {});
                            });

            const auto registerDirectDepthPre = [this, &registerBuiltin](std::string_view type) {
                registerBuiltin(std::string(type),
                                {},
                                {"depth"},
                                [this](FrameGraph&,
                                       FrameGraphBlackboard&,
                                       const vrendergraph::ParamBlock&,
                                       vrendergraph::PassBuildContext& passCtx) {
                                    auto* ctx = m_Owner.m_CurrentBuildContext;
                                    if (!ctx)
                                        return;
                                    m_DirectGBufferPass.addDepthPrePass(*ctx);
                                    if (auto depth = ctx->data.tryGet(kResKey_DepthTexture))
                                    {
                                        passCtx.setOutput("depth", depth);
                                        if (ctx->view().stereoMode != StereoRenderMode::eMono)
                                            ctx->data.set(kResKey_StereoDepth, depth);
                                    }
                                });
            };
            registerDirectDepthPre("DirectDepthPre");
            registerDirectDepthPre("DepthPre");

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
                    settings.depthBias                 = params.get<float>("depthBias", settings.depthBias);
                    settings.normalBias                = params.get<float>("normalBias", settings.normalBias);
                    settings.pcssLightRadius           = params.get<float>("pcssLightRadius", settings.pcssLightRadius);
                    const auto* shadowDirectionalLight = findPrimaryShadowDirectionalLight(ctx->view().renderWorld);
                    settings.enabled                   = settings.enabled && shadowDirectionalLight != nullptr;
                    if (shadowDirectionalLight)
                    {
                        settings.lightDirection = shadowDirectionalLight->direction;
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
                    const bool suppressCameraSkybox =
                        ctx->view().camera != nullptr && ctx->view().camera->suppressSkybox;
                    rhi::Texture* skyboxTexture =
                        !suppressCameraSkybox && settings.pbrLighting.showSkybox ? settings.pbrLighting.environmentMap :
                                                                                    nullptr;
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
                        const bool cameraWantsSkybox =
                            !suppressCameraSkybox &&
                            ((ctx->view().camera && ctx->view().camera->clearMode == 1u) ||
                             settings.pbrLighting.showSkybox);
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
                                if (!settings.enabled)
                                {
                                    passCtx.setOutput("ao", {});
                                    return;
                                }
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
                                if (!settings.enabled)
                                {
                                    passCtx.setOutput("reflection", {});
                                    return;
                                }
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

            registerBuiltin("UiOverlay",
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
                                auto color = m_UiOverlayPass.addPass(*ctx, passCtx.getInput("source"));
                                if (color)
                                {
                                    ctx->data.set(kResKey_FinalCompositionSource, color);
                                    passCtx.setOutput("color", color);
                                }
                                else
                                {
                                    passCtx.setOutput("color", passCtx.getInput("source"));
                                }
                            });

            registerBuiltin("XrGeometryWarp",
                            {"source", "depth"},
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

                                XrViewSynthesisSettings settings;
                                settings.enabled    = true;
                                settings.sourceView = params.get<std::string>("sourceView", settings.sourceView);
                                settings.targetView = params.get<std::string>("targetView", settings.targetView);
                                settings.gridSize = static_cast<uint32_t>(std::max(params.get<int>("gridSize", 4), 1));
                                settings.warpStrength =
                                    std::max(params.get<float>("warpStrength", settings.warpStrength), 0.0f);
                                auto color = m_XrGeometryWarpPass.addPass(
                                    *ctx, passCtx.getInput("source"), passCtx.getInput("depth"), settings);
                                if (color)
                                {
                                    ctx->data.set(kResKey_FinalCompositionSource, color);
                                    if (ctx->view().stereoMode != StereoRenderMode::eMono)
                                        ctx->data.set(kResKey_StereoColor, color);
                                    passCtx.setOutput("color", color);
                                }
                            });

            registerBuiltin("XrPullPushInpaint",
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

                                XrViewSynthesisSettings settings;
                                auto                    color =
                                    m_XrPullPushInpaintPass.addPass(*ctx, passCtx.getInput("source"), settings);
                                if (color)
                                {
                                    ctx->data.set(kResKey_FinalCompositionSource, color);
                                    if (ctx->view().stereoMode != StereoRenderMode::eMono)
                                        ctx->data.set(kResKey_StereoColor, color);
                                    passCtx.setOutput("color", color);
                                }
                            });

            registerBuiltin("FinalComposition",
                            {"source"},
                            {"target"},
                            [this](FrameGraph& fg,
                                   FrameGraphBlackboard&,
                                   const vrendergraph::ParamBlock&,
                                   vrendergraph::PassBuildContext& passCtx) {
                                auto* ctx = m_Owner.m_CurrentBuildContext;
                                if (!ctx || !ctx->view().target)
                                    return;
                                auto source = passCtx.getInput("source");
                                if (!source)
                                {
                                    warnMissingPassInputOnce("FinalComposition", "source");
                                    return;
                                }
                                const auto* outputRef      = passCtx.getOutputRef("target");
                                const auto  outputName     = outputRef && isBackbufferResource(outputRef->resource) ?
                                                                 outputRef->resource :
                                                                 "target";
                                const auto  outputSelector = outputRef ? outputRef->selector : nlohmann::json::object();
                                ctx->data.set(kResKey_FinalCompositionSource, source);
                                auto target = m_FinalCompositionPass.compose(
                                    *ctx,
                                    importRenderGraphBackbuffer(
                                        fg, ctx->view(), outputName, outputSelector, "VRenderGraphBackbuffer"));
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
                            {"visibility", "depth"},
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
                                if (auto depth = ctx->data.tryGet(kResKey_DepthTexture))
                                    passCtx.setOutput("depth", depth);
                            });

            registerBuiltin("ThinGBuffer",
                            {"visibility", "depth"},
                            {"color", "normal", "material", "depth", "entityId"},
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
                                if (auto res = ctx->data.tryGet(kResKey_GBufferMaterial))
                                    passCtx.setOutput("material", res);
                                passCtx.setOutput("depth", passCtx.getInput("depth"));
                                if (auto res = ctx->data.tryGet(kResKey_GBufferEntityId))
                                    passCtx.setOutput("entityId", res);
                                else
                                    passCtx.setOutput("entityId", {});
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
                            {"visibleInstance", "visibleInstanceCount", "meshletCullDispatchArgs"},
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
                            {"visibleMeshlet", "visibleMeshletCount"},
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
                            {"draw", "meshlets"},
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

            registerBuiltin("GeneralGaussianSplatComposite",
                            {"source"},
                            {"color"},
                            [this](FrameGraph&,
                                   FrameGraphBlackboard&,
                                   const vrendergraph::ParamBlock&,
                                   vrendergraph::PassBuildContext& passCtx) {
                                auto* ctx = m_Owner.m_CurrentBuildContext;
                                if (!ctx)
                                    return;

                                const auto source       = passCtx.getInput("source");
                                auto*      gpuSceneView = ctx->view().gpuSceneView;
                                if (!gpuSceneView || !gpuSceneView->hasGeneralGaussianSplats())
                                {
                                    passCtx.setOutput("color", source);
                                    return;
                                }

                                ctx->data.set(kResKey_FinalCompositionSource, source);
                                m_GaussianPreprocessPass.addPass(*ctx);
                                auto color = m_GaussianRenderPass.addPass(*ctx);
                                if (color)
                                {
                                    ctx->data.set(kResKey_FinalCompositionSource, color);
                                    passCtx.setOutput("color", color);
                                }
                                else
                                {
                                    passCtx.setOutput("color", source);
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
        std::unordered_map<std::string, std::unique_ptr<FullscreenPassRuntime>> m_ProjectPassRuntimes;
        std::unordered_map<std::string, std::unique_ptr<ComputePassRuntime>>    m_ProjectComputePassRuntimes;
        std::unordered_set<std::string>                                         m_UnsupportedRayTracingPasses;
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
        UiOverlayPass                                                           m_UiOverlayPass;
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
        XrGeometryWarpPass                                                      m_XrGeometryWarpPass;
        XrPullPushInpaintPass                                                   m_XrPullPushInpaintPass;
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
                runtime->fullscreen = std::make_unique<FullscreenPassRuntime>(
                    pass, vertexLibrary, fragmentLibrary);
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
                const bool builtinGraph = m_PipelineUri.starts_with("builtin://");
                const auto json         = nlohmann::json::parse(text.value());
                static_cast<void>(vrendergraph::loadRenderGraph(json));
                auto feature        = Feature {};
                feature.name        = m_PipelineUri;
                feature.renderGraph = m_PipelineUri;
                m_Asset.rendererKey = m_RendererKeyOverride.empty() ? rendererKeyFromRenderGraphUri(m_PipelineUri) :
                                                                      m_RendererKeyOverride;
                if (!builtinGraph)
                {
                    m_Asset.shaderLibraries.try_emplace("project", "res://shaders/project.vshaderlib.lua");
                    loadProjectGraphPasses();
                }
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
            const auto defaultVertexLibrary =
                pass.shader.vertex == "fullscreen_triangle.vert" ? std::string {"builtin"} : pass.shader.library;
            pass.shader.vertexLibrary =
                getString(shaderTable, "vertexLibrary", getString(shaderTable, "vertex_library", defaultVertexLibrary));
            pass.shader.fragmentLibrary =
                getString(shaderTable, "fragmentLibrary", getString(shaderTable, "fragment_library", pass.shader.library));
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

        sol::object shaderObj = table["shader"];
        if (!shaderObj.is<sol::table>())
            return false;

        sol::table shaderTable     = shaderObj.as<sol::table>();
        outPass.shader.library     = getString(shaderTable, "library", "project");
        outPass.shader.vertex      = getString(shaderTable, "vertex", "fullscreen_triangle.vert");
        outPass.shader.fragment    = getString(shaderTable, "fragment");
        const auto defaultVertexLibrary =
            outPass.shader.vertex == "fullscreen_triangle.vert" ? std::string {"builtin"} : outPass.shader.library;
        outPass.shader.vertexLibrary =
            getString(shaderTable, "vertexLibrary", getString(shaderTable, "vertex_library", defaultVertexLibrary));
        outPass.shader.fragmentLibrary =
            getString(shaderTable, "fragmentLibrary", getString(shaderTable, "fragment_library", outPass.shader.library));
        outPass.shader.compute     = getString(shaderTable, "compute");
        outPass.shader.raygen      = getString(shaderTable, "raygen");
        outPass.shader.miss        = getString(shaderTable, "miss");
        outPass.shader.closestHit  = getString(shaderTable, "closestHit");
        outPass.shader.anyHit      = getString(shaderTable, "anyHit");

        const auto pipeline = normalizeId(getString(table, "pipeline", getString(table, "stage")));
        if (pipeline == "compute" || !outPass.shader.compute.empty())
            outPass.pipeline = ProjectGraphPass::Pipeline::eCompute;
        else if (pipeline == "raytracing" || pipeline == "ray_tracing" || pipeline == "rt" ||
                 !outPass.shader.raygen.empty())
            outPass.pipeline = ProjectGraphPass::Pipeline::eRayTracing;
        else
            outPass.pipeline = ProjectGraphPass::Pipeline::eGraphics;

        const bool hasInputList  = table["inputs"].valid();
        const bool hasOutputList = table["outputs"].valid();
        outPass.inputs           = getStringList(table, "inputs");
        outPass.outputs          = getStringList(table, "outputs");

        if (outPass.inputs.empty() && (!hasInputList || outPass.pipeline == ProjectGraphPass::Pipeline::eGraphics))
            outPass.inputs.push_back(getString(table, "input", "source"));
        if (outPass.outputs.empty() && (!hasOutputList || outPass.pipeline == ProjectGraphPass::Pipeline::eGraphics))
            outPass.outputs.push_back(getString(table, "output", "color"));

        if (outPass.pipeline == ProjectGraphPass::Pipeline::eCompute)
        {
            auto dispatchX = getInt(table, "dispatchX", static_cast<int>(outPass.dispatchX));
            auto dispatchY = getInt(table, "dispatchY", static_cast<int>(outPass.dispatchY));
            auto dispatchZ = getInt(table, "dispatchZ", static_cast<int>(outPass.dispatchZ));
            outPass.dispatchByOutputSize = getBool(table, "dispatchByOutputSize", outPass.dispatchByOutputSize);

            sol::object dispatchObj = table["dispatch"];
            if (dispatchObj.is<sol::table>())
            {
                sol::table dispatch = dispatchObj.as<sol::table>();
                dispatchX = getInt(dispatch, "x", dispatchX);
                dispatchY = getInt(dispatch, "y", dispatchY);
                dispatchZ = getInt(dispatch, "z", dispatchZ);
                outPass.dispatchByOutputSize =
                    getBool(dispatch, "byOutputSize", outPass.dispatchByOutputSize);
            }

            outPass.dispatchX = static_cast<uint32_t>(std::max(dispatchX, 1));
            outPass.dispatchY = static_cast<uint32_t>(std::max(dispatchY, 1));
            outPass.dispatchZ = static_cast<uint32_t>(std::max(dispatchZ, 1));

            if (outPass.outputs.empty())
                outPass.outputs.push_back(getString(table, "output", "color"));
            return !outPass.shader.compute.empty();
        }

        if (outPass.pipeline == ProjectGraphPass::Pipeline::eRayTracing)
            return !outPass.shader.raygen.empty();

        auto& pass         = outPass.fullscreen;
        pass.name          = getString(table, "passName", outPass.type);
        pass.shader        = outPass.shader;
        pass.input         = outPass.inputs.front();
        pass.output        = outPass.outputs.front();
        return !pass.shader.vertex.empty() && !pass.shader.fragment.empty();
    }

    void DeclarativeRenderer::loadProjectGraphPasses()
    {
        auto* services     = getServices();
        auto* assetService = services ? services->tryGet<IAssetService>() : nullptr;
        if (!assetService)
            return;

        std::unordered_set<std::string> loadedLogicalPaths;
        const auto parsePassText = [this](std::string_view label, std::string_view text) {
            if (text.find("RenderGraphPass") == std::string_view::npos)
                return;

            auto lua    = makeAssetLuaState();
            auto result = lua.safe_script(std::string(text), &sol::script_pass_on_error);
            if (!result.valid())
            {
                sol::error err = result;
                VULTRA_CORE_ERROR("[DeclarativeRenderer] Failed to parse project graph pass '{}': {}", label, err.what());
                return;
            }

            sol::object obj = result;
            if (!obj.is<sol::table>())
                return;

            ProjectGraphPass pass;
            if (parseProjectGraphPassTable(obj.as<sol::table>(), pass))
                m_Asset.projectGraphPasses.push_back(std::move(pass));
        };

        const auto assetRoot = std::filesystem::path(assetService->resolveUri("res://")).lexically_normal();
        const bool shouldScanPhysicalAssetRoot =
            !assetRoot.empty() && assetRoot != assetRoot.root_path() && assetRoot != assetRoot.root_name();
        std::error_code ec;
        if (shouldScanPhysicalAssetRoot && std::filesystem::is_directory(assetRoot, ec))
        {
            std::vector<std::filesystem::path> files;
            for (auto it = std::filesystem::recursive_directory_iterator(
                     assetRoot, std::filesystem::directory_options::skip_permission_denied, ec);
                 it != std::filesystem::recursive_directory_iterator {};
                 it.increment(ec))
            {
                if (ec)
                {
                    ec.clear();
                    continue;
                }
                const auto& entry = *it;
                if (!entry.is_regular_file(ec) || entry.path().extension() != ".lua")
                {
                    ec.clear();
                    continue;
                }
                std::error_code relEc;
                const auto      rel = std::filesystem::relative(entry.path(), assetRoot, relEc);
                if (relEc || rel.empty() || isImportedAssetPath(rel.generic_string()))
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

                std::error_code relEc;
                const auto      rel = std::filesystem::relative(file, assetRoot, relEc);
                if (!relEc && !rel.empty())
                    loadedLogicalPaths.insert(rel.generic_string());

                parsePassText(file.generic_string(), buffer.str());
            }
        }

        std::vector<std::string> registryPassUris;
        for (const auto& [_, entry] : assetService->registry().getRegistry())
        {
            static_cast<void>(_);
            if (entry.type != vasset::VAssetType::eScriptLua || entry.sourcePath.empty())
                continue;

            const auto logicalPath = std::filesystem::path(entry.sourcePath).generic_string();
            if (!logicalPath.ends_with(".lua") || isImportedAssetPath(logicalPath) || loadedLogicalPaths.contains(logicalPath))
            {
                continue;
            }

            registryPassUris.push_back(uriFromLogicalPath(logicalPath));
        }
        std::sort(registryPassUris.begin(), registryPassUris.end());
        registryPassUris.erase(std::unique(registryPassUris.begin(), registryPassUris.end()), registryPassUris.end());

        for (const auto& uri : registryPassUris)
        {
            auto text = assetService->loadTextAssetSync(uri);
            if (!text)
            {
                VULTRA_CORE_ERROR(
                    "[DeclarativeRenderer] Failed to load project graph pass '{}': {}", uri, std::move(text).error());
                continue;
            }
            parsePassText(uri, text.value());
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
