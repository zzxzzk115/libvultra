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
#include "vultra/function/rendering/srp/builtin/passes/debug_draw_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/deferred_lighting_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/depth_pre_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/direct_gbuffer_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/drawset_build_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/final_composition_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/fxaa_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/general_gaussian_splat_foveated_composite_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/bloom_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/gaussian_blur_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/general_gaussian_splat_preprocess_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/general_gaussian_splat_render_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/hzb_generate_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/meshlet_cull_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/meshlet_hiz_cull_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/particle_render_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/particle_simulate_pass.hpp"
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
            lua.set_function("ShadingModel", [](sol::table t) { return t; });
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

    namespace
    {
        // Single source of truth for the builtin render-graph pass catalog: every pass type's
        // input/output slot names and parameter descriptors. Both the editor-facing registry
        // (registerBuiltinRenderGraphPasses) and the runtime renderer's registry derive their
        // port/param layout from here, so a slot only ever needs to be declared once.
        //
        // `pass` is any callable of the shape
        //   (std::string type, std::vector<std::string> inputs, std::vector<std::string> outputs,
        //    std::vector<vrendergraph::ParamDesc> params = {})
        template<typename PassFn>
        void declareBuiltinRenderGraphPasses(PassFn&& pass)
        {
        pass("CameraClear", {}, {"color"});
        pass("CompatibilityBaseColor", {}, {"color"});
        pass("DirectGBuffer", {"depth"}, {"color", "depth", "normal", "material", "emissive", "entityId"});
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
        pass("DeferredLighting",
             {"color", "normal", "material", "emissive", "depth", "ao", "shadowMap", "shadowData"},
             {"color"});
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
        pass("GaussianBlur",
             {"source"},
             {"color"},
             {
                 {.name = "scale", .type = vrendergraph::ParamType::eFloat, .defaultValue = 1.0f},
                 // direction: 0 = both (horizontal then vertical), 1 = horizontal only, 2 = vertical only
                 {.name = "direction", .type = vrendergraph::ParamType::eInt, .defaultValue = 0},
             });
        pass("Bloom",
             {"source"},
             {"color"},
             {
                 {.name = "enabled", .type = vrendergraph::ParamType::eBoolean, .defaultValue = true},
                 {.name = "threshold", .type = vrendergraph::ParamType::eFloat, .defaultValue = 1.0f},
                 {.name = "knee", .type = vrendergraph::ParamType::eFloat, .defaultValue = 0.5f},
                 {.name = "intensity", .type = vrendergraph::ParamType::eFloat, .defaultValue = 0.6f},
                 {.name = "scale", .type = vrendergraph::ParamType::eFloat, .defaultValue = 1.0f},
                 {.name = "iterations", .type = vrendergraph::ParamType::eInt, .defaultValue = 1},
             });
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
        pass("DebugDraw",
             {"source", "depth"},
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
        pass("ThinGBuffer", {"visibility", "depth"}, {"color", "normal", "material", "emissive", "depth", "entityId"});
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
        pass("ParticleRender", {"source", "depth"}, {"color"});
        }
    } // namespace

    void registerBuiltinRenderGraphPasses(vrendergraph::RenderGraphRegistry& registry)
    {
        const auto noop =
            [](FrameGraph&, FrameGraphBlackboard&, const vrendergraph::ParamBlock&, vrendergraph::PassBuildContext&) {};
        declareBuiltinRenderGraphPasses([&](std::string                          type,
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
        });
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
                                   const vrendergraph::ParamBlock& params       = {},
                                   std::vector<FrameGraphResource> extraInputs = {})
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
                        const auto extraDesc = ctx.fg.getDescriptor<framegraph::FrameGraphTexture>(extra);
                        const bool depthInput = isDepthFormat(extraDesc.format);
                        (void)builder.read(extra,
                                           framegraph::TextureRead {
                                               .binding =
                                                   {
                                                       .location = {.set = 3, .binding = extraBinding},
                                                       .pipelineStage = framegraph::PipelineStage::eFragmentShader,
                                                   },
                                               .type = framegraph::TextureRead::Type::eCombinedImageSampler,
                                               .imageAspect =
                                                   depthInput ? rhi::ImageAspect::eDepth : rhi::ImageAspect::eColor,
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
        loadScriptedShader(rhi::ShaderLibraryRuntime& lib, const std::string& id, const vshadersystem::ShaderStage stage)
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
                        const uint32_t val =
                            v.is<int>() ? static_cast<uint32_t>(std::max(v.as<int>(), 0)) : fb;
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
        struct ScriptedPassFrameData
        {
            ScriptedShaderSelection shader;
            // Extent of the last created output texture, used by dispatchByOutputSize().
            rhi::Extent2D outputExtent {0, 0};
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
                m_Builder(&builder),
                m_Ctx(&ctx),
                m_PassCtx(&passCtx),
                m_Params(&params),
                m_Frame(&frame),
                m_Env(env),
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
                auto usage = rhi::ImageUsage::eSampled | rhi::ImageUsage::eTransferSrc;
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

            void read(const LuaResHandle& h, sol::table binding)
            {
                check(h);
                const uint32_t set        = static_cast<uint32_t>(getInt(binding, "set", 3));
                const uint32_t bindingIx  = static_cast<uint32_t>(getInt(binding, "binding", 0));
                const auto     stageName  = getString(binding, "stage", "fragment");
                const auto     stage      = scriptedPipelineStage(stageName);
                const bool     depth      = getBool(binding, "depth", false);
                // Compute shaders sample via texelFetch (sampled image); fragment
                // shaders use a combined image sampler. Match the existing runtimes.
                const bool     isCompute  = stage == framegraph::PipelineStage::eComputeShader;
                (void)m_Builder->read(
                    h.resource,
                    framegraph::TextureRead {
                        .binding = {.location = {.set = set, .binding = bindingIx}, .pipelineStage = stage},
                        .type    = isCompute ? framegraph::TextureRead::Type::eSampledImage :
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
                        .clearValue  = clear.value_or(false) ?
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
                               const ScriptedPassFrameData& frame,
                               ScriptedPassPipelines&       pipelines) :
                m_Rc(&rc), m_Frame(&frame), m_Pipelines(&pipelines)
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
                m_Rc->cb.pushConstants(
                    scriptedRhiStage(stage), 0, static_cast<uint32_t>(bytes.size()), bytes.data());
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

        private:
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
                const auto      type = normalizeId(getString(entry, "type", "float"));
                sol::object     def  = entry["default"];
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
            for (auto& [_, pipelines] : m_ScriptedPassPipelines)
            {
                if (pipelines)
                    pipelines->invalidate();
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

            auto* shaderService =
                m_Owner.getServices() ? m_Owner.getServices()->tryGet<IShaderService>() : nullptr;
            if (!shaderService)
                return;

            auto& pipelines = m_ScriptedPassPipelines[def.type];
            if (!pipelines)
                pipelines = std::make_unique<ScriptedPassPipelines>();

            const ScriptedPassEnv env {shaderService, &m_Owner.m_Asset.shaderLibraries, def.sourcePath};
            const uint64_t        generation = ++s_ScriptedPassGeneration;
            const sol::protected_function setupFn  = def.setup;
            const sol::protected_function execFn   = def.execute;
            const std::string             passType = def.type;

            ctx.fg.addCallbackPass<ScriptedPassFrameData>(
                def.type.c_str(),
                [&ctx, &passCtx, &params, env, generation, setupFn, passType](FrameGraph::Builder& builder,
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
                [execFn, passType, pipelinesPtr = pipelines.get()](
                    const ScriptedPassFrameData& frame, FrameGraphPassResources&, void* ctxPtr) {
                    VULTRA_SCOPED_FRAMEGRAPH_EXEC_CONTEXT(rc, ctxPtr);
                    assertRenderScriptThread();
                    LuaPassExecContext execCtx(rc, frame, *pipelinesPtr);
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
            auto* regShaderService =
                m_Owner.getServices() ? m_Owner.getServices()->tryGet<IShaderService>() : nullptr;
            const auto resolveLibAtReg = [this, regShaderService](const std::string& name) -> rhi::ShaderLibraryRuntime* {
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
                    passParams = reflectShaderParams(def.reflectLibrary, def.reflectFragment,
                                                     vshadersystem::ShaderStage::eFrag);
                else if (!def.reflectCompute.empty())
                    passParams = reflectShaderParams(def.reflectLibrary, def.reflectCompute,
                                                     vshadersystem::ShaderStage::eComp);
                for (const auto& pd : def.params)
                {
                    auto it = std::find_if(passParams.begin(), passParams.end(),
                                           [&](const auto& e) { return e.name == pd.name; });
                    if (it != passParams.end())
                        *it = pd;
                    else
                        passParams.push_back(pd);
                }

                m_Registry.registerPass(vrendergraph::PassDefinition {
                    .type  = def.type,
                    .setup = [this, scriptedIndex](FrameGraph&,
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

            // Port/param layout comes from the single-source declareBuiltinRenderGraphPasses()
            // catalog; here we only attach the runtime setup callback by pass type.
            std::unordered_map<std::string, vrendergraph::PassDefinition> builtinSpecs;
            declareBuiltinRenderGraphPasses([&builtinSpecs](std::string                          type,
                                                            std::vector<std::string>             inputs,
                                                            std::vector<std::string>             outputs,
                                                            std::vector<vrendergraph::ParamDesc> params = {}) {
                vrendergraph::PassDefinition def {};
                def.type    = type;
                def.inputs  = std::move(inputs);
                def.outputs = std::move(outputs);
                def.params  = std::move(params);
                builtinSpecs.emplace(std::move(type), std::move(def));
            });

            const auto registerBuiltin = [this, &builtinSpecs](std::string               type,
                                                               vrendergraph::PassSetupFn setup) {
                // A scripted/custom pass of the same name registered earlier wins.
                if (m_Registry.contains(type))
                    return;
                const auto it = builtinSpecs.find(type);
                if (it == builtinSpecs.end())
                {
                    VULTRA_CORE_ERROR("[DeclarativeRenderer] No port spec for builtin pass '{}' "
                                      "(declare it in declareBuiltinRenderGraphPasses)",
                                      type);
                    return;
                }
                auto def  = it->second;
                def.setup = std::move(setup);
                m_Registry.registerPass(std::move(def));
            };

            registerBuiltin("CameraClear",
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
                                if (auto res = ctx->data.tryGet(kResKey_GBufferEmissive))
                                    passCtx.setOutput("emissive", res);
                                if (auto res = ctx->data.tryGet(kResKey_GBufferEntityId))
                                    passCtx.setOutput("entityId", res);
                                else
                                    passCtx.setOutput("entityId", {});
                            });

            const auto registerDirectDepthPre = [this, &registerBuiltin](std::string_view type) {
                registerBuiltin(std::string(type),
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

            registerBuiltin("ShadowMap",
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

            registerBuiltin("DeferredLighting",
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

            registerBuiltin("GaussianBlur",
                            [this](FrameGraph&,
                                   FrameGraphBlackboard&,
                                   const vrendergraph::ParamBlock& params,
                                   vrendergraph::PassBuildContext& passCtx) {
                                auto* ctx = m_Owner.m_CurrentBuildContext;
                                if (!ctx)
                                    return;
                                const auto  source    = passCtx.getInput("source");
                                const float scale     = params.get<float>("scale", 1.0f);
                                const int   direction = params.get<int>("direction", 0);
                                FrameGraphResource color {};
                                if (direction == 1)
                                    color = m_GaussianBlurPass.addPass(*ctx, source, scale, true);
                                else if (direction == 2)
                                    color = m_GaussianBlurPass.addPass(*ctx, source, scale, false);
                                else
                                    color = m_GaussianBlurPass.addPass(*ctx, source, scale);
                                if (color)
                                    passCtx.setOutput("color", color);
                            });

            registerBuiltin("Bloom",
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
                                auto color = m_BloomPass.addPass(*ctx,
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
                            });

            registerBuiltin("ToneMapping",
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

            registerBuiltin("DebugDraw",
                            [this](FrameGraph&,
                                   FrameGraphBlackboard&,
                                   const vrendergraph::ParamBlock& params,
                                   vrendergraph::PassBuildContext& passCtx) {
                                auto* ctx = m_Owner.m_CurrentBuildContext;
                                auto* renderService =
                                    m_Owner.getServices() ? m_Owner.getServices()->tryGet<IRenderService>() : nullptr;
                                const auto* camera = ctx ? ctx->view().camera : nullptr;
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
                                auto color = m_DebugDrawPass.addPass(*ctx,
                                                                     passCtx.getInput("source"),
                                                                     passCtx.getInput("depth"),
                                                                     debugProjection * camera->view);
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

            registerBuiltin("UiOverlay",
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
                                // Pull scene depth from the frame data registry (published by the
                                // depth/gbuffer pass) so world-space UI is occluded by geometry;
                                // it is optional, so graphs without a depth pass just skip occlusion.
                                const auto depth = ctx->data.tryGet(kResKey_DepthTexture);
                                auto color = m_UiOverlayPass.addPass(*ctx, passCtx.getInput("source"), depth);
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
                                if (auto res = ctx->data.tryGet(kResKey_GBufferEmissive))
                                    passCtx.setOutput("emissive", res);
                                passCtx.setOutput("depth", passCtx.getInput("depth"));
                                if (auto res = ctx->data.tryGet(kResKey_GBufferEntityId))
                                    passCtx.setOutput("entityId", res);
                                else
                                    passCtx.setOutput("entityId", {});
                            });

            registerBuiltin("CoarseInstanceCull",
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

            registerBuiltin("ParticleRender",
                            [this](FrameGraph&,
                                   FrameGraphBlackboard&,
                                   const vrendergraph::ParamBlock&,
                                   vrendergraph::PassBuildContext& passCtx) {
                                auto* ctx = m_Owner.m_CurrentBuildContext;
                                if (!ctx)
                                    return;

                                const auto source = passCtx.getInput("source");
                                const auto depth  = passCtx.getInput("depth");

                                auto* gpuSceneView = ctx->view().gpuSceneView;
                                if (!gpuSceneView || gpuSceneView->particleEmitters.empty())
                                {
                                    passCtx.setOutput("color", source);
                                    return;
                                }

                                // Simulate (compute) then draw (billboards). The simulate pass returns
                                // the per-emitter pool handles so the render pass reads them with a
                                // correct compute-write -> vertex-read barrier.
                                auto particleBuffers = m_ParticleSimulatePass.addPass(*ctx);
                                auto color = m_ParticleRenderPass.addPass(*ctx, source, depth, particleBuffers);
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
        GaussianBlurPass                                                        m_GaussianBlurPass;
        BloomPass                                                               m_BloomPass;
        FxaaPass                                                                m_FxaaPass;
        ToneMappingPass                                                         m_ToneMappingPass;
        SelectionOutlinePass                                                    m_SelectionOutlinePass;
        DebugDrawPass                                                           m_DebugDrawPass;
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
        ParticleSimulatePass                                                    m_ParticleSimulatePass;
        ParticleRenderPass                                                      m_ParticleRenderPass;
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
                    loadScriptedPasses();
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
        m_ShadingModelRegistry.clear();
        for (const auto& model : m_Asset.shadingModels)
        {
            const uint32_t code = m_ShadingModelRegistry.registerModel(model);
            VULTRA_CORE_INFO("[DeclarativeRenderer] Registered custom shading model '{}' as code {}", model.name, code);
        }
        loadScriptedPasses();
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

        // Custom shading models: `shadingModels = { ShadingModel{ ... }, ... }`.
        sol::object modelsObj = table["shadingModels"];
        if (modelsObj.is<sol::table>())
        {
            sol::table models = modelsObj.as<sol::table>();
            for (const auto& [_, value] : models)
            {
                static_cast<void>(_);
                material::ShadingModelDesc model;
                if (value.is<sol::table>() && parseShadingModelTable(value.as<sol::table>(), model))
                    outAsset.shadingModels.push_back(std::move(model));
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

    bool DeclarativeRenderer::parseShadingModelTable(sol::table table, material::ShadingModelDesc& outModel)
    {
        outModel.name = getString(table, "name");
        if (outModel.name.empty())
            return false;
        outModel.bxdfLibrary  = getString(table, "bxdfLibrary", "project");
        outModel.bxdfArtifact = getString(table, "bxdfArtifact");
        outModel.bxdfFunction = getString(table, "bxdfFunction");
        outModel.extraParamSize = static_cast<uint32_t>(std::max(0, getInt(table, "extraParamSize", 0)));
        // defaultExtraParams (a typed param block) is left empty here; it is wired with
        // the ShadingModelParamsBuffer when the forward custom-BXDF shading path lands.
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

    void DeclarativeRenderer::loadScriptedPasses()
    {
        auto* services     = getServices();
        auto* assetService = services ? services->tryGet<IAssetService>() : nullptr;
        if (!assetService)
            return;
        auto* shaderService = services ? services->tryGet<IShaderService>() : nullptr;

        // Pass-definition diagnostics are rebuilt on every (re)load. Shader-
        // resolution diagnostics are re-published per frame from each pass's setup.
        if (shaderService)
            shaderService->clearRenderPassDiagnostics();

        std::unordered_set<std::string> loadedLogicalPaths;
        const auto parsePassText = [this, shaderService](std::string_view label,
                                                         std::string_view text,
                                                         std::string      sourcePath) {
            // Report (and log) an invalid pass definition against its source .lua so
            // the code editor can mark it. label is for the log, sourcePath keys the UI.
            const auto reportDefinitionError = [&](std::string message) {
                VULTRA_CORE_ERROR("[DeclarativeRenderer] Invalid render pass '{}': {}", label, message);
                if (shaderService && !sourcePath.empty())
                    shaderService->setRenderPassDiagnostics(sourcePath,
                                                            {AssetDiagnostic {sourcePath, 0, 0, std::move(message)}});
            };

            if (text.find("RenderGraphPass") == std::string_view::npos)
                return;

            // Run in the persistent render-script state inside a fresh environment:
            // a scripted pass returns `setup`/`execute` sol::functions that must
            // outlive parsing (they execute every frame), and per-file environments
            // keep each pass file's globals from leaking into the next.
            auto&            lua = renderScriptState();
            sol::environment env(lua, sol::create, lua.globals());
            auto             result = lua.safe_script(std::string(text), env, &sol::script_pass_on_error);
            if (!result.valid())
            {
                const sol::error err = result;
                reportDefinitionError(std::string {"Lua error: "} + err.what());
                return;
            }

            sol::object obj = result;
            if (!obj.is<sol::table>())
            {
                reportDefinitionError("script must return a RenderGraphPass { ... } table.");
                return;
            }

            sol::table table = obj.as<sol::table>();
            if (table["setup"].get_type() == sol::type::function &&
                table["execute"].get_type() == sol::type::function)
            {
                ScriptedPassDef def;
                if (parseScriptedPassTable(table, def))
                {
                    def.sourcePath = sourcePath;
                    m_Asset.scriptedPasses.push_back(std::move(def));
                }
                else
                {
                    reportDefinitionError("missing a required 'type' (or 'name') field.");
                }
                return;
            }

            reportDefinitionError("missing setup/execute functions. See doc/scripted_render_passes.md.");
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
                std::string     logical;
                if (!relEc && !rel.empty())
                {
                    logical = rel.generic_string();
                    loadedLogicalPaths.insert(logical);
                }

                parsePassText(file.generic_string(), buffer.str(), logical);
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
            parsePassText(uri, text.value(), uri);
        }
    }

    bool DeclarativeRenderer::loadShaderLibraries()
    {
        auto* services      = getServices();
        auto* shaderService = services ? services->tryGet<IShaderService>() : nullptr;
        if (!shaderService)
            return false;

        for (const auto& [name, uri] : m_Asset.shaderLibraries)
        {
            if (!shaderService->reloadProjectLibrary(uri))
            {
                // The project shader library is an optional override: when it isn't present the
                // builtin shaders are used. A genuinely malformed library is still error-logged by
                // the shader system, so treat a load miss here as non-fatal.
                VULTRA_CORE_TRACE(
                    "[DeclarativeRenderer] Shader library '{}' ('{}') not loaded; using builtin shaders.", name, uri);
            }
        }
        return true;
    }
} // namespace vultra
