#include "editor_app/ui/windows/render_graph_window.hpp"

#include "editor_app/project_asset_utils.hpp"
#include "editor_app/ui/graph_layout.hpp"
#include "editor_app/ui/texture_preview_utils.hpp"
#include "common/ui_widgets.hpp"

#include <vultra/core/rhi/sampler.hpp>
#include <vultra/function/rendering/runtime_profiler.hpp>
#include <vultra/function/rendering/render_structs.hpp>
#include <vultra/function/rendering/srp/builtin/builtin_rendergraph_registry.hpp>
#include <vultra/function/rendering/srp/declarative_renderer.hpp>
#include <vultra/function/services/asset_service.hpp>
#include <vultra/function/services/camera_service.hpp>
#include <vultra/function/services/imgui_service.hpp>
#include <vultra/function/services/render_backend_service.hpp>
#include <vultra/function/services/render_service.hpp>
#include <vultra/function/services/shader_service.hpp>
#include <vultra/function/services/world_service.hpp>
#include <vultra/function/world/components/camera_component.hpp>
#include <vultra/function/world/components/hierarchy_component.hpp>
#include <vultra/function/world/components/id_component.hpp>
#include <vultra/function/world/components/name_component.hpp>
#include <vultra/function/world/components/transform_component.hpp>
#include <vultra/function/world/world.hpp>

#include <vrendergraph/vrendergraph.hpp>

#include <IconsMaterialDesignIcons.h>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>
#include <imgui_graphnode/imgui_graphnode.h>
#include <imnodes/imnodes.h>
#include <nlohmann/json.hpp>
#include <sol/sol.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <entt/entity/entity.hpp>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace vultra_app
{
    namespace
    {
        constexpr uint64_t kRenderTargetReleaseDelayFrames = 3;
        constexpr float    kOverlayZoomMin                 = 0.5f;
        constexpr float    kOverlayZoomMax                 = 4.0f;
        constexpr float    kOverlayZoomStep                = 0.25f;
        constexpr float    kRenderGraphPreviewAspect        = 16.0f / 9.0f;
        constexpr uint32_t kRuntimeGraphThumbnailMaxExtent  = 160;

        class EditorCpuScope
        {
        public:
            EditorCpuScope(const EditorContext&, std::string_view name) : m_Scope(name) {}

        private:
            vultra::RuntimeProfiler::ExternalScope m_Scope;
        };

        struct ResRef
        {
            std::string node;
            std::string slot;
        };

        uint32_t fnv1a32(std::string_view text)
        {
            uint32_t h = 2166136261u;
            for (const unsigned char c : text)
            {
                h ^= uint32_t(c);
                h *= 16777619u;
            }
            return h == 0 ? 1 : h;
        }

        int stableId(std::string_view text) { return static_cast<int>(fnv1a32(text) & 0x7fffffffu); }

        std::optional<ResRef> parseResRef(std::string_view ref)
        {
            if (ref.empty())
                return std::nullopt;

            const auto dot = ref.find('.');
            if (dot == std::string_view::npos)
                return ResRef {std::string(ref), "out"};

            if (dot == 0 || dot + 1 >= ref.size())
                return std::nullopt;
            return ResRef {std::string(ref.substr(0, dot)), std::string(ref.substr(dot + 1))};
        }

        std::string makeResRef(std::string_view node, std::string_view slot)
        {
            if (slot == "out")
                return std::string(node);
            return std::string(node) + "." + std::string(slot);
        }

        struct PassPorts
        {
            std::vector<std::string> inputs;
            std::vector<std::string> outputs;
        };

        PassPorts collectPassPorts(const vrendergraph::RenderGraphRegistry& registry,
                                   const vrendergraph::PassDecl&            pass)
        {
            if (registry.contains(pass.type))
            {
                const auto& def = registry.get(pass.type);
                return {.inputs = def.inputs, .outputs = def.outputs};
            }

            PassPorts ports;
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

        vrendergraph::PassDecl* findPass(vrendergraph::RenderGraphDesc& graph, std::string_view id)
        {
            for (auto& pass : graph.passes)
            {
                if (pass.id == id)
                    return &pass;
            }
            return nullptr;
        }

        const vrendergraph::PassDecl* findPass(const vrendergraph::RenderGraphDesc& graph, std::string_view id)
        {
            for (const auto& pass : graph.passes)
            {
                if (pass.id == id)
                    return &pass;
            }
            return nullptr;
        }

        bool hasResource(const vrendergraph::RenderGraphDesc& graph, std::string_view name)
        {
            return std::any_of(
                graph.resources.begin(), graph.resources.end(), [&](const auto& r) { return r.name == name; });
        }

        bool passTypeHasOutput(const vrendergraph::RenderGraphRegistry& registry,
                               const vrendergraph::PassDecl&            pass,
                               std::string_view                         slot)
        {
            if (!registry.contains(pass.type))
                return false;
            const auto& outputs = registry.get(pass.type).outputs;
            return std::find(outputs.begin(), outputs.end(), slot) != outputs.end();
        }

        const vrendergraph::PassDecl* findUniquePassByTypeWithOutput(const vrendergraph::RenderGraphRegistry& registry,
                                                                      const vrendergraph::RenderGraphDesc&     graph,
                                                                      std::string_view                         type,
                                                                      std::string_view                         slot)
        {
            const vrendergraph::PassDecl* match = nullptr;
            for (const auto& pass : graph.passes)
            {
                if (pass.type != type || !passTypeHasOutput(registry, pass, slot))
                    continue;
                if (match)
                    return nullptr;
                match = &pass;
            }
            return match;
        }

        bool repairMissingInputRefs(const vrendergraph::RenderGraphRegistry& registry,
                                    vrendergraph::RenderGraphDesc&           graph)
        {
            bool changed = false;
            for (auto& pass : graph.passes)
            {
                for (auto& [slot, ref] : pass.inputs)
                {
                    static_cast<void>(slot);
                    auto parsed = parseResRef(ref);
                    if (!parsed || findPass(graph, parsed->node) || hasResource(graph, parsed->node))
                        continue;

                    if (const auto* replacement =
                            findUniquePassByTypeWithOutput(registry, graph, parsed->node, parsed->slot))
                    {
                        ref = makeResRef(replacement->id, parsed->slot);
                    }
                    else
                    {
                        ref.clear();
                    }
                    changed = true;
                }
            }
            return changed;
        }

        void removeDefaultOutputRefs(vrendergraph::RenderGraphDesc& graph)
        {
            for (auto& pass : graph.passes)
            {
                if (pass.outputs.empty())
                    continue;

                bool allDefault = true;
                for (const auto& [slot, value] : pass.outputs)
                {
                    if (!value.selector.empty() || value.resource != makeResRef(pass.id, slot))
                    {
                        allDefault = false;
                        break;
                    }
                }
                if (allDefault)
                    pass.outputs.clear();
            }
        }

        void applyRenderGraphAutoLayout(const vrendergraph::RenderGraphRegistry& registry,
                                        vrendergraph::RenderGraphDesc&           graph)
        {
            std::unordered_map<std::string, bool> hasOutgoing;
            for (const auto& pass : graph.passes)
                hasOutgoing.try_emplace(pass.id, false);

            std::vector<GraphLayoutEdge> edges;
            for (const auto& pass : graph.passes)
            {
                for (const auto& [slot, ref] : pass.inputs)
                {
                    static_cast<void>(slot);
                    const auto parsed = parseResRef(ref);
                    if (!parsed || !findPass(graph, parsed->node))
                        continue;
                    int fromOrder = 0;
                    if (const auto* source = findPass(graph, parsed->node); source && registry.contains(source->type))
                    {
                        const auto& outputs = registry.get(source->type).outputs;
                        const auto  outIt   = std::find(outputs.begin(), outputs.end(), parsed->slot);
                        if (outIt != outputs.end())
                            fromOrder = static_cast<int>(std::distance(outputs.begin(), outIt));
                    }

                    int toOrder = 0;
                    if (registry.contains(pass.type))
                    {
                        const auto& inputs = registry.get(pass.type).inputs;
                        const auto  it     = std::find(inputs.begin(), inputs.end(), slot);
                        if (it != inputs.end())
                            toOrder = static_cast<int>(std::distance(inputs.begin(), it));
                    }
                    edges.push_back({
                        .from      = parsed->node,
                        .to        = pass.id,
                        .fromOrder = fromOrder,
                        .toOrder   = toOrder,
                    });
                    hasOutgoing[parsed->node] = true;
                }
            }

            std::vector<GraphLayoutNode> nodes;
            nodes.reserve(graph.passes.size());
            for (size_t i = 0; i < graph.passes.size(); ++i)
            {
                const auto& pass        = graph.passes[i];
                const auto* def         = registry.contains(pass.type) ? &registry.get(pass.type) : nullptr;
                const int   inputCount  = def ? static_cast<int>(def->inputs.size()) : 0;
                const int   outputCount = def ? static_cast<int>(def->outputs.size()) : 0;
                const int   paramCount  = def ? static_cast<int>(def->params.size()) : 0;
                nodes.push_back(GraphLayoutNode {
                    .id          = pass.id,
                    .order       = static_cast<int>(i),
                    .inputCount  = inputCount,
                    .outputCount = outputCount,
                    .heightLanes = std::max(
                        1.0f,
                        (124.0f + static_cast<float>(std::max(inputCount, outputCount) + paramCount) * 31.0f) / 260.0f),
                    .sink = !hasOutgoing[pass.id],
                });
            }

            const auto layout = computeLayeredGraphLayout(nodes,
                                                          edges,
                                                          GraphLayoutConfig {
                                                              .origin           = {80.0f, 80.0f},
                                                              .columnSpacing    = 560.0f,
                                                              .rowSpacing       = 260.0f,
                                                              .sinkExtraSpacing = 260.0f,
                                                              .laneGap          = 0.80f,
                                                          });

            if (!graph.meta.is_object())
                graph.meta = nlohmann::json::object();
            auto& metaNodes = graph.meta["editor"]["nodes"];
            if (!metaNodes.is_object())
                metaNodes = nlohmann::json::object();

            for (const auto& [id, pos] : layout)
                metaNodes[id]["pos"] = nlohmann::json::array({pos.x, pos.y});
        }

        void ensureSlots(vrendergraph::PassDecl& pass, const vrendergraph::PassDefinition& def)
        {
            for (const auto& slot : def.inputs)
                pass.inputs.try_emplace(slot, "");
            for (const auto& param : def.params)
            {
                auto& raw = pass.params.raw();
                if (!raw.contains(param.name))
                    raw[param.name] = param.defaultValue;
            }
        }

        bool applyTopoOrder(vrendergraph::RenderGraphDesc& graph, std::string* error)
        {
            std::unordered_map<std::string, size_t> order;
            for (size_t i = 0; i < graph.passes.size(); ++i)
                order[graph.passes[i].id] = i;

            std::unordered_map<std::string, std::vector<std::string>> edges;
            std::unordered_map<std::string, int>                      indegree;
            for (const auto& pass : graph.passes)
                indegree.try_emplace(pass.id, 0);

            for (const auto& pass : graph.passes)
            {
                for (const auto& [slot, ref] : pass.inputs)
                {
                    (void)slot;
                    auto parsed = parseResRef(ref);
                    if (!parsed || !findPass(graph, parsed->node))
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

            if (sorted.size() != graph.passes.size())
            {
                if (error)
                    *error = "Render graph contains a pass dependency cycle.";
                return false;
            }

            std::vector<vrendergraph::PassDecl> reordered;
            reordered.reserve(graph.passes.size());
            for (const auto& id : sorted)
                reordered.push_back(*findPass(graph, id));
            graph.passes = std::move(reordered);
            return true;
        }

        vrendergraph::RenderGraphDesc makeActiveGraphWithPassthrough(const vrendergraph::RenderGraphRegistry& registry,
                                                                     const vrendergraph::RenderGraphDesc&     graph)
        {
            auto activeGraph = graph;
            bool changed     = true;
            while (changed)
            {
                changed = false;
                for (const auto& pass : activeGraph.passes)
                {
                    if (pass.enabled)
                        continue;

                    const auto ports = collectPassPorts(registry, pass);
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

                        const auto disabledOutput = makeResRef(pass.id, outputSlot);
                        for (auto& dst : activeGraph.passes)
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

            activeGraph.passes.erase(std::remove_if(activeGraph.passes.begin(),
                                                    activeGraph.passes.end(),
                                                    [](const auto& pass) { return !pass.enabled; }),
                                     activeGraph.passes.end());
            return activeGraph;
        }

        bool validateRenderGraph(const vrendergraph::RenderGraphRegistry& registry,
                                 const vrendergraph::RenderGraphDesc&     graph,
                                 std::string&                             error)
        {
            auto activeGraph = makeActiveGraphWithPassthrough(registry, graph);

            std::unordered_set<std::string> resources;
            for (const auto& resource : activeGraph.resources)
            {
                if (resource.name.empty())
                {
                    error = "Render graph has an unnamed external resource.";
                    return false;
                }
                if (!resources.insert(resource.name).second)
                {
                    error = "Render graph has duplicate resource: " + resource.name;
                    return false;
                }
            }

            std::unordered_map<std::string, const vrendergraph::PassDecl*> passes;
            for (const auto& pass : activeGraph.passes)
            {
                if (pass.id.empty())
                {
                    error = "Render graph has an unnamed pass.";
                    return false;
                }
                if (!passes.emplace(pass.id, &pass).second)
                {
                    error = "Render graph has duplicate pass: " + pass.id;
                    return false;
                }
                if (!registry.contains(pass.type))
                {
                    error = "Unknown pass type '" + pass.type + "' on pass '" + pass.id + "'.";
                    return false;
                }

                const auto&                           def = registry.get(pass.type);
                const std::unordered_set<std::string> validInputs(def.inputs.begin(), def.inputs.end());
                const std::unordered_set<std::string> validOutputs(def.outputs.begin(), def.outputs.end());
                const auto isOptionalInput = [&](const std::string& slot) {
                    return pass.type == "DirectGBuffer" && slot == "depth";
                };
                for (const auto& slot : def.inputs)
                {
                    auto it = pass.inputs.find(slot);
                    if (it == pass.inputs.end() || it->second.empty())
                    {
                        if (isOptionalInput(slot))
                            continue;
                        error = "Pass '" + pass.id + "' input '" + slot + "' is not connected.";
                        return false;
                    }
                }
                for (const auto& [slot, ref] : pass.inputs)
                {
                    if (!validInputs.contains(slot))
                    {
                        error = "Pass '" + pass.id + "' has unknown input slot '" + slot + "'.";
                        return false;
                    }
                    auto parsed = parseResRef(ref);
                    if (!parsed)
                    {
                        error = "Pass '" + pass.id + "' input '" + slot + "' has invalid resource ref.";
                        return false;
                    }
                    if (!resources.contains(parsed->node) && !passes.contains(parsed->node))
                    {
                        error = "Pass '" + pass.id + "' input '" + slot + "' references missing node '" + parsed->node +
                                "'.";
                        return false;
                    }
                    if (passes.contains(parsed->node))
                    {
                        const auto& src = *passes.at(parsed->node);
                        if (!registry.contains(src.type))
                            continue;
                        const auto& srcDef = registry.get(src.type);
                        if (std::find(srcDef.outputs.begin(), srcDef.outputs.end(), parsed->slot) ==
                            srcDef.outputs.end())
                        {
                            error = "Pass '" + pass.id + "' input '" + slot + "' references missing output slot '" +
                                    parsed->slot + "' on pass '" + parsed->node + "'.";
                            return false;
                        }
                    }
                }
                for (const auto& [slot, ref] : pass.outputs)
                {
                    (void)ref;
                    if (!validOutputs.contains(slot))
                    {
                        error = "Pass '" + pass.id + "' has unknown output slot '" + slot + "'.";
                        return false;
                    }
                }
            }

            vrendergraph::RenderGraphDesc copy = activeGraph;
            if (!applyTopoOrder(copy, &error))
                return false;
            return true;
        }

        void drawGraphStatusBanner(std::string_view message, const bool error)
        {
            if (message.empty())
                return;

            const ImVec4 bg     = error ? ImVec4 {0.34f, 0.08f, 0.06f, 0.95f} : ImVec4 {0.08f, 0.16f, 0.24f, 0.95f};
            const ImVec4 border = error ? ImVec4 {0.95f, 0.22f, 0.16f, 1.0f} : ImVec4 {0.22f, 0.48f, 0.78f, 1.0f};
            const ImVec4 text   = error ? ImVec4 {1.0f, 0.84f, 0.80f, 1.0f} : ImVec4 {0.82f, 0.92f, 1.0f, 1.0f};

            ImGui::PushStyleColor(ImGuiCol_ChildBg, bg);
            ImGui::PushStyleColor(ImGuiCol_Border, border);
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
            const float height = std::max(
                34.0f,
                ImGui::CalcTextSize(
                    message.data(), message.data() + message.size(), false, ImGui::GetContentRegionAvail().x - 20.0f)
                        .y +
                    18.0f);
            if (ImGui::BeginChild("##RenderGraphStatusBanner",
                                  ImVec2(0.0f, height),
                                  true,
                                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
            {
                ImGui::PushStyleColor(ImGuiCol_Text, text);
                ImGui::TextWrapped("%s", std::string(message).c_str());
                ImGui::PopStyleColor();
            }
            ImGui::EndChild();
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(2);
        }

        struct ParamEnumOption
        {
            std::string label;
            int         value {0};
        };

        struct EditorShaderRef
        {
            std::string                library {"project"};
            vultra::rhi::ShaderProfile profile {vultra::rhi::ShaderProfile::eGeneral};
            std::string                fragment;
            std::string                compute;
        };

        struct EditorProjectGraphPassDesc
        {
            std::string              type;
            std::vector<std::string> inputs;
            std::vector<std::string> outputs;
            EditorShaderRef          shader;
            bool                     hasShader {false};
        };

        std::string projectCachePrefix(const EditorContext& ctx)
        {
            return ctx.state.currentProject.lexically_normal().generic_string() + "|" + ctx.state.currentAssetRoot +
                   "|" + std::to_string(ctx.state.assetFileGeneration);
        }

        std::string shaderRefCacheKey(const EditorShaderRef& shaderRef)
        {
            return shaderRef.library + "|" + std::to_string(static_cast<int>(shaderRef.profile)) + "|" +
                   shaderRef.fragment + "|" + shaderRef.compute;
        }

        std::string primaryShaderId(const EditorShaderRef& shaderRef)
        {
            return !shaderRef.fragment.empty() ? shaderRef.fragment : shaderRef.compute;
        }

        vshadersystem::ShaderStage primaryShaderStage(const EditorShaderRef& shaderRef)
        {
            return !shaderRef.fragment.empty() ? vshadersystem::ShaderStage::eFrag : vshadersystem::ShaderStage::eComp;
        }

        std::vector<ParamEnumOption>
        readShaderParamEnum(const EditorContext& ctx, const vrendergraph::PassDecl& pass, std::string_view paramName);
        std::vector<vrendergraph::ParamDesc> readShaderParamDescs(const EditorContext&          ctx,
                                                                  const vrendergraph::PassDecl& pass);

        bool isEditorVisiblePassPin(const vrendergraph::PassDecl& pass, const std::string& slot, bool input)
        {
            if (!input && pass.type == "FinalComposition" && slot == "target")
                return false;
            return true;
        }

        float textWidth(std::string_view text) { return ImGui::CalcTextSize(text.data(), text.data() + text.size()).x; }

        float valuePreviewWidth(const nlohmann::json& value)
        {
            if (value.is_string())
                return textWidth(value.get<std::string>());
            if (value.is_boolean())
                return value.get<bool>() ? textWidth("true") : textWidth("false");
            if (value.is_number_integer())
                return textWidth(std::to_string(value.get<int>()));
            if (value.is_number_float())
                return textWidth(std::to_string(value.get<float>()));
            return textWidth(value.dump());
        }

        void ensureParamDefaults(vrendergraph::PassDecl& pass, const vrendergraph::PassDefinition& def)
        {
            auto& raw = pass.params.raw();
            for (const auto& param : def.params)
            {
                if (!raw.contains(param.name))
                    raw[param.name] = param.defaultValue;
            }
        }

        void drawParamField(EditorContext&                 ctx,
                            vrendergraph::PassDecl&        pass,
                            const vrendergraph::ParamDesc& param,
                            bool&                          dirty,
                            const float                    labelWidth,
                            const float                    valueWidth)
        {
            const EditorCpuScope perf {ctx, "Editor::RenderGraph/CanvasParamField"};
            auto& raw = pass.params.raw();
            if (!raw.contains(param.name))
                raw[param.name] = param.defaultValue;

            if (param.name == "name" || param.name == "library" || param.name == "vertex" || param.name == "fragment" ||
                param.name == "pushConstants")
                return;

            ImGui::PushID(param.name.c_str());
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(param.name.c_str());
            ImGui::SameLine(labelWidth);
            ImGui::PushItemWidth(valueWidth);

            if (param.type == vrendergraph::ParamType::eFloat)
            {
                float value = raw[param.name].get<float>();
                if (ImGui::DragFloat("##value", &value, 0.01f))
                {
                    if (param.minValue)
                        value = std::max(value, param.minValue->get<float>());
                    if (param.maxValue)
                        value = std::min(value, param.maxValue->get<float>());
                    raw[param.name] = value;
                    dirty           = true;
                }
            }
            else if (param.type == vrendergraph::ParamType::eInt)
            {
                int        value       = raw[param.name].get<int>();
                const auto enumOptions = readShaderParamEnum(ctx, pass, param.name);
                if (!enumOptions.empty())
                {
                    const char* preview = nullptr;
                    for (const auto& option : enumOptions)
                    {
                        if (option.value == value)
                        {
                            preview = option.label.c_str();
                            break;
                        }
                    }
                    if (!preview)
                        preview = "Unknown";

                    if (ImGui::BeginCombo("##value", preview))
                    {
                        for (const auto& option : enumOptions)
                        {
                            const bool selected = option.value == value;
                            if (ImGui::Selectable(option.label.c_str(), selected))
                            {
                                raw[param.name] = option.value;
                                dirty           = true;
                            }
                            if (selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                }
                else if (ImGui::DragInt("##value", &value, 1))
                {
                    if (param.minValue)
                        value = std::max(value, param.minValue->get<int>());
                    if (param.maxValue)
                        value = std::min(value, param.maxValue->get<int>());
                    raw[param.name] = value;
                    dirty           = true;
                }
            }
            else if (param.type == vrendergraph::ParamType::eBoolean)
            {
                bool value = raw[param.name].get<bool>();
                if (ImGui::Checkbox("##value", &value))
                {
                    raw[param.name] = value;
                    dirty           = true;
                }
            }
            else
            {
                const auto value = raw[param.name].get<std::string>();
                struct StringOption
                {
                    std::string_view label;
                    std::string_view value;
                };

                std::vector<StringOption> options;
                if (!options.empty())
                {
                    std::string preview = value;
                    for (const auto& option : options)
                    {
                        if (value == option.value)
                        {
                            preview = std::string(option.label);
                            break;
                        }
                    }

                    if (ImGui::BeginCombo("##value", preview.c_str()))
                    {
                        for (const auto& option : options)
                        {
                            const bool selected = value == option.value;
                            if (ImGui::Selectable(std::string(option.label).c_str(), selected))
                            {
                                raw[param.name] = std::string(option.value);
                                dirty           = true;
                            }
                            if (selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                }
                else
                {
                    std::array<char, 256> buffer {};
                    std::snprintf(buffer.data(), buffer.size(), "%s", value.c_str());
                    if (ImGui::InputText("##value", buffer.data(), buffer.size()))
                    {
                        raw[param.name] = std::string(buffer.data());
                        dirty           = true;
                    }
                }
            }

            ImGui::PopItemWidth();
            ImGui::PopID();
        }

        float passNodeWidth(const EditorContext&                ctx,
                            const vrendergraph::PassDecl&       pass,
                            const vrendergraph::PassDefinition& def)
        {
            float width = std::max(textWidth(pass.id), textWidth(pass.type)) + 58.0f;
            for (const auto& slot : def.inputs)
                width = std::max(width, textWidth(slot) + 150.0f);
            for (const auto& slot : def.outputs)
            {
                if (isEditorVisiblePassPin(pass, slot, false))
                    width = std::max(width, textWidth(slot) + 150.0f);
            }

            auto measureParam = [&](const vrendergraph::ParamDesc& param) {
                if (param.name == "name" || param.name == "library" || param.name == "vertex" ||
                    param.name == "fragment" || param.name == "pushConstants")
                    return;
                const auto& raw     = pass.params.raw();
                const auto  valueIt = raw.find(param.name);
                const float value =
                    valueIt == raw.end() ? valuePreviewWidth(param.defaultValue) : valuePreviewWidth(*valueIt);
                width = std::max(width, textWidth(param.name) + std::clamp(value + 58.0f, 150.0f, 280.0f) + 50.0f);
            };
            auto params = def.params;
            for (const auto& param : readShaderParamDescs(ctx, pass))
            {
                if (std::find_if(params.begin(), params.end(), [&](const auto& existing) {
                        return existing.name == param.name;
                    }) == params.end())
                    params.push_back(param);
            }
            for (const auto& param : params)
                measureParam(param);
            return std::clamp(width, 250.0f, 540.0f);
        }

        void pushNodePalette(ImU32 title, ImU32 body)
        {
            ImNodes::PushColorStyle(ImNodesCol_TitleBar, title);
            ImNodes::PushColorStyle(ImNodesCol_TitleBarHovered, title);
            ImNodes::PushColorStyle(ImNodesCol_TitleBarSelected, title);
            ImNodes::PushColorStyle(ImNodesCol_NodeBackground, body);
            ImNodes::PushColorStyle(ImNodesCol_NodeBackgroundHovered, body);
            ImNodes::PushColorStyle(ImNodesCol_NodeBackgroundSelected, body);
        }

        void popNodePalette()
        {
            ImNodes::PopColorStyle();
            ImNodes::PopColorStyle();
            ImNodes::PopColorStyle();
            ImNodes::PopColorStyle();
            ImNodes::PopColorStyle();
            ImNodes::PopColorStyle();
        }

        void pushNodeTitlePalette(ImU32 title)
        {
            ImVec4 hovered = ImGui::ColorConvertU32ToFloat4(title);
            hovered.x      = std::min(hovered.x + 0.08f, 1.0f);
            hovered.y      = std::min(hovered.y + 0.08f, 1.0f);
            hovered.z      = std::min(hovered.z + 0.08f, 1.0f);

            ImVec4 selected = ImGui::ColorConvertU32ToFloat4(title);
            selected.x      = std::min(selected.x + 0.15f, 1.0f);
            selected.y      = std::min(selected.y + 0.15f, 1.0f);
            selected.z      = std::min(selected.z + 0.15f, 1.0f);

            ImNodes::PushColorStyle(ImNodesCol_TitleBar, title);
            ImNodes::PushColorStyle(ImNodesCol_TitleBarHovered, ImGui::ColorConvertFloat4ToU32(hovered));
            ImNodes::PushColorStyle(ImNodesCol_TitleBarSelected, ImGui::ColorConvertFloat4ToU32(selected));
        }

        void popNodeTitlePalette()
        {
            ImNodes::PopColorStyle();
            ImNodes::PopColorStyle();
            ImNodes::PopColorStyle();
        }

        void drawNodeTitleText(const char* text, const float fontSize = 18.0f)
        {
            const ImVec2 pos      = ImGui::GetCursorScreenPos();
            ImDrawList*  drawList = ImGui::GetWindowDrawList();
            ImFont*      font     = ImGui::GetFont();
            const ImU32  outline  = IM_COL32(0, 0, 0, 220);
            const ImU32  main     = ImGui::GetColorU32(ImGuiCol_Text);

            drawList->AddText(font, fontSize, ImVec2(pos.x - 1.0f, pos.y), outline, text);
            drawList->AddText(font, fontSize, ImVec2(pos.x + 1.0f, pos.y), outline, text);
            drawList->AddText(font, fontSize, ImVec2(pos.x, pos.y - 1.0f), outline, text);
            drawList->AddText(font, fontSize, ImVec2(pos.x, pos.y + 1.0f), outline, text);
            drawList->AddText(font, fontSize, pos, main, text);

            const float  scale    = fontSize / ImGui::GetFontSize();
            const ImVec2 textSize = ImGui::CalcTextSize(text);
            ImGui::Dummy(ImVec2 {textSize.x * scale, textSize.y * scale});
        }

        void pushLinkPalette(ImU32 color)
        {
            ImNodes::PushColorStyle(ImNodesCol_Link, color);
            ImNodes::PushColorStyle(ImNodesCol_LinkHovered, color);
            ImNodes::PushColorStyle(ImNodesCol_LinkSelected, color);
        }

        void popLinkPalette()
        {
            ImNodes::PopColorStyle();
            ImNodes::PopColorStyle();
            ImNodes::PopColorStyle();
        }

        vultra::rhi::Sampler makeLinearClampSampler(EditorContext& ctx)
        {
            auto* backendService = ctx.services ? ctx.services->tryGet<vultra::IRenderBackendService>() : nullptr;
            if (!backendService)
                return {};

            return backendService->renderDevice().getSampler(vultra::rhi::SamplerInfo {
                .magFilter    = vultra::rhi::TexelFilter::eLinear,
                .minFilter    = vultra::rhi::TexelFilter::eLinear,
                .mipmapMode   = vultra::rhi::MipmapMode::eLinear,
                .addressModeS = vultra::rhi::SamplerAddressMode::eClampToEdge,
                .addressModeT = vultra::rhi::SamplerAddressMode::eClampToEdge,
                .addressModeR = vultra::rhi::SamplerAddressMode::eClampToEdge,
            });
        }

        ImU32 vrgNodeColorFromType(std::string_view type, const bool resource)
        {
            if (resource)
                return IM_COL32(230, 140, 40, 255);

            std::string lower(type);
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            if (lower.find("depth") != std::string::npos)
                return IM_COL32(180, 180, 180, 255);
            if (type == "Present")
                return IM_COL32(80, 200, 120, 255);

            uint32_t h = 2166136261u;
            for (const unsigned char c : type)
            {
                h ^= uint32_t(c);
                h *= 16777619u;
            }

            ImVec4 color {};
            ImGui::ColorConvertHSVtoRGB(float(h % 360) / 360.0f, 0.55f, 0.85f, color.x, color.y, color.z);
            color.w = 1.0f;
            return ImGui::ColorConvertFloat4ToU32(color);
        }

        bool writeTextAtomic(const std::filesystem::path& path, std::string_view text, std::string& error)
        {
            std::error_code ec;
            if (path.has_parent_path())
            {
                std::filesystem::create_directories(path.parent_path(), ec);
                if (ec)
                {
                    error = "Failed to create directory: " + path.parent_path().generic_string();
                    return false;
                }
            }

            const auto tmp = path.parent_path() / (path.filename().generic_string() + ".tmp");
            {
                std::ofstream file(tmp, std::ios::binary | std::ios::trunc);
                if (!file.is_open())
                {
                    error = "Failed to open temp file: " + tmp.generic_string();
                    return false;
                }
                file.write(text.data(), static_cast<std::streamsize>(text.size()));
                if (!file.good())
                {
                    error = "Failed to write temp file: " + tmp.generic_string();
                    return false;
                }
            }

            std::filesystem::remove(path, ec);
            ec.clear();
            std::filesystem::rename(tmp, path, ec);
            if (ec)
            {
                std::filesystem::remove(tmp);
                error = "Failed to replace file: " + path.generic_string();
                return false;
            }
            return true;
        }

        std::vector<std::string> parsePipelineFeatureRefs(std::string_view lua)
        {
            std::vector<std::string> out;

            const auto featuresPos = lua.find("features");
            if (featuresPos == std::string_view::npos)
                return out;

            const auto openBrace = lua.find('{', featuresPos);
            if (openBrace == std::string_view::npos)
                return out;

            int    depth = 0;
            size_t end   = openBrace;
            for (; end < lua.size(); ++end)
            {
                if (lua[end] == '{')
                    ++depth;
                else if (lua[end] == '}')
                {
                    if (--depth == 0)
                        break;
                }
            }
            if (end <= openBrace)
                return out;

            const auto block = lua.substr(openBrace + 1, end - openBrace - 1);
            for (size_t i = 0; i < block.size(); ++i)
            {
                if (block[i] != '"' && block[i] != '\'')
                    continue;

                const char  quote = block[i++];
                std::string value;
                while (i < block.size() && block[i] != quote)
                {
                    if (block[i] == '\\' && i + 1 < block.size())
                        ++i;
                    value.push_back(block[i++]);
                }
                if (!value.empty())
                    out.push_back(std::move(value));
            }

            return out;
        }

        std::string escapeLuaString(std::string_view value)
        {
            std::string out;
            out.reserve(value.size() + 8);
            for (const char ch : value)
            {
                if (ch == '\\' || ch == '"')
                    out.push_back('\\');
                out.push_back(ch);
            }
            return out;
        }

        std::string serializeFeatureBlock(const std::vector<std::string>& features)
        {
            std::string out = "{\n";
            for (const auto& feature : features)
                out += "        \"" + escapeLuaString(feature) + "\",\n";
            out += "    }";
            return out;
        }

        std::optional<std::string> replacePipelineFeatureRefs(std::string_view                lua,
                                                              const std::vector<std::string>& features)
        {
            const auto featuresPos = lua.find("features");
            if (featuresPos == std::string_view::npos)
                return std::nullopt;

            const auto openBrace = lua.find('{', featuresPos);
            if (openBrace == std::string_view::npos)
                return std::nullopt;

            int    depth = 0;
            size_t end   = openBrace;
            for (; end < lua.size(); ++end)
            {
                if (lua[end] == '{')
                    ++depth;
                else if (lua[end] == '}')
                {
                    if (--depth == 0)
                        break;
                }
            }
            if (end <= openBrace || end >= lua.size())
                return std::nullopt;

            std::string out(lua.substr(0, openBrace));
            out += serializeFeatureBlock(features);
            out += std::string(lua.substr(end + 1));
            return out;
        }

        const std::array<std::string_view, 6> kBuiltinPipelineFeatures {
            "compatibility_basecolor",
            "direct_gbuffer",
            "meshlet",
            "general_gaussian_splat",
            "builtin_screen_space",
            "final_composition",
        };

        void registerBuiltinRenderGraphResources(vrendergraph::RenderGraphRegistry& registry)
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
                registry.registerResource(name);
        }

        void registerEditorBuiltinRenderGraphPasses(vrendergraph::RenderGraphRegistry& registry)
        {
            vultra::registerBuiltinRenderGraphPasses(registry);
        }

        std::string trim(std::string_view text)
        {
            const auto first = text.find_first_not_of(" \t\r\n");
            if (first == std::string_view::npos)
                return {};
            const auto last = text.find_last_not_of(" \t\r\n");
            return std::string(text.substr(first, last - first + 1));
        }

        std::string solString(sol::table table, const char* key, std::string fallback = {})
        {
            sol::object value = table[key];
            return value.is<std::string>() ? value.as<std::string>() : std::move(fallback);
        }

        std::vector<std::string> solStringList(sol::table table, const char* key)
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
            return out;
        }

        std::string runtimeCameraDisplayName(std::string_view cameraName)
        {
            if (cameraName == "Scene View")
                return "Editor Camera";
            if (cameraName.empty())
                return "Game Camera";
            return "Game Camera: " + std::string(cameraName);
        }

        std::optional<std::filesystem::path> findShaderSourceFile(const EditorContext& ctx, std::string_view shaderId)
        {
            if (ctx.state.currentProject.empty() || shaderId.empty())
                return std::nullopt;

            const auto root   = (ctx.state.currentProject / ctx.state.currentAssetRoot / "shaders").lexically_normal();
            const auto wanted = std::string(shaderId);
            const auto wantedVShader = wanted.ends_with(".vshader") ? wanted : wanted + ".vshader";
            std::error_code ec;
            for (const auto& entry : std::filesystem::recursive_directory_iterator(root, ec))
            {
                if (ec)
                    break;
                if (!entry.is_regular_file(ec))
                    continue;

                const auto rel = std::filesystem::relative(entry.path(), root, ec).generic_string();
                if (rel == wantedVShader || entry.path().filename().generic_string() == wantedVShader)
                    return entry.path().lexically_normal();
            }
            return std::nullopt;
        }

        std::optional<std::filesystem::path> findBuiltinShaderSourceFile(const EditorShaderRef& shaderRef)
        {
            const auto shaderId = primaryShaderId(shaderRef);
            if (shaderId.empty())
                return std::nullopt;

            const auto profileDir = [&]() -> std::string {
                switch (shaderRef.profile)
                {
                    case vultra::rhi::ShaderProfile::eCompatibility:
                        return "compatibility";
                    case vultra::rhi::ShaderProfile::eHighend:
                        return "highend";
                    case vultra::rhi::ShaderProfile::eGeneral:
                    default:
                        return "general";
                }
            }();

            std::vector<std::filesystem::path> roots;
            auto                               cwd = std::filesystem::current_path();
            roots.push_back(cwd);
            roots.push_back(cwd.parent_path());
            roots.push_back(cwd.parent_path().parent_path());

            std::vector<std::filesystem::path> candidates;
            const auto                         shaderPath        = std::filesystem::path(shaderId);
            const auto                         shaderPathVShader = shaderId.ends_with(".vshader") ?
                                                                       shaderPath :
                                                                       std::filesystem::path(shaderId + ".vshader");
            for (const auto& root : roots)
            {
                candidates.push_back(root / "builtin" / "shaders" / "passes" / profileDir / shaderPathVShader);
                candidates.push_back(root / "builtin" / "shaders" / "passes" / profileDir / shaderPath);
            }

            std::error_code ec;
            for (const auto& candidate : candidates)
            {
                if (std::filesystem::is_regular_file(candidate, ec))
                    return candidate.lexically_normal();
                ec.clear();
            }
            return std::nullopt;
        }

        std::optional<std::filesystem::path> findShaderSourceFile(const EditorContext&   ctx,
                                                                  const EditorShaderRef& shaderRef)
        {
            if (shaderRef.library == "builtin")
                return findBuiltinShaderSourceFile(shaderRef);
            return findShaderSourceFile(ctx, primaryShaderId(shaderRef));
        }

        std::string assetUriForPath(const EditorContext& ctx, const std::filesystem::path& path)
        {
            if (ctx.state.currentProject.empty())
                return {};
            std::error_code ec;
            const auto      assetRoot = (ctx.state.currentProject / ctx.state.currentAssetRoot).lexically_normal();
            const auto      rel       = std::filesystem::relative(path.lexically_normal(), assetRoot, ec);
            if (ec || rel.empty())
                return {};
            return "res://" + rel.generic_string();
        }

        std::filesystem::path assetPathForUri(const EditorContext& ctx, std::string_view uri)
        {
            if (uri.empty())
                return {};
            constexpr std::string_view prefix = "res://";
            if (ctx.state.currentProject.empty())
                return {};
            if (!uri.starts_with(prefix))
                return {};
            return (ctx.state.currentProject / ctx.state.currentAssetRoot /
                    std::filesystem::path(std::string(uri.substr(prefix.size()))))
                .lexically_normal();
        }

        std::vector<std::string> collectProjectRenderGraphUris(const EditorContext& ctx)
        {
            return collectProjectAssetUrisWithSuffix(ctx.state.currentProject, ctx.state.currentAssetRoot, ".vrg.json");
        }

        std::vector<std::string> collectBuiltinRenderGraphUris()
        {
            std::vector<std::string> out;
            for (const auto& source : vultra::builtinRenderGraphSources())
                out.emplace_back(source.uri);
            std::sort(out.begin(), out.end());
            return out;
        }

        std::string rendererKeyFromRenderGraphUri(std::string_view uri)
        {
            auto                       filename = std::filesystem::path(std::string(uri)).filename().generic_string();
            constexpr std::string_view suffix   = ".vrg.json";
            if (filename.ends_with(suffix))
                filename.resize(filename.size() - suffix.size());
            if (filename.empty())
                filename = "custom";
            for (auto& ch : filename)
            {
                if (ch == '-' || ch == ' ')
                    ch = '_';
                else
                    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }
            return filename;
        }

        bool drawProjectRenderGraphSelector(std::vector<std::string>&  cachedUris,
                                            std::filesystem::path&     cachedProject,
                                            std::string&               cachedAssetRoot,
                                            uint64_t&                  cachedAssetGeneration,
                                            EditorContext&             ctx,
                                            std::string&               status)
        {
            const EditorCpuScope perf {ctx, "Editor::RenderGraph/AssetSelector"};
            const bool cacheDirty =
                cachedProject != ctx.state.currentProject || cachedAssetRoot != ctx.state.currentAssetRoot ||
                cachedAssetGeneration != ctx.state.assetFileGeneration;
            if (cacheDirty)
            {
                const EditorCpuScope refreshPerf {ctx, "Editor::RenderGraph/AssetSelectorRefresh"};
                cachedUris               = collectProjectRenderGraphUris(ctx);
                auto builtinUris         = collectBuiltinRenderGraphUris();
                cachedUris.insert(cachedUris.end(), builtinUris.begin(), builtinUris.end());
                std::sort(cachedUris.begin(), cachedUris.end());
                cachedUris.erase(std::unique(cachedUris.begin(), cachedUris.end()), cachedUris.end());
                cachedProject            = ctx.state.currentProject;
                cachedAssetRoot          = ctx.state.currentAssetRoot;
                cachedAssetGeneration    = ctx.state.assetFileGeneration;
            }

            auto uris = cachedUris;
            if (!ctx.state.currentEditingRenderGraph.empty() &&
                std::find(uris.begin(), uris.end(), ctx.state.currentEditingRenderGraph) == uris.end())
            {
                uris.push_back(ctx.state.currentEditingRenderGraph);
                std::sort(uris.begin(), uris.end());
            }

            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Graph");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(320.0f);

            bool        changed = false;
            const char* preview =
                ctx.state.currentEditingRenderGraph.empty() ? "<none>" : ctx.state.currentEditingRenderGraph.c_str();
            if (ImGui::BeginCombo("##ProjectRenderGraphAsset", preview))
            {
                for (const auto& uri : uris)
                {
                    const bool selected = uri == ctx.state.currentEditingRenderGraph;
                    if (ImGui::Selectable(uri.c_str(), selected))
                    {
                        ctx.state.currentEditingRenderGraph = uri;
                        changed                             = true;
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            if (changed)
            {
                if (auto* renderService = ctx.services ? ctx.services->tryGet<vultra::IRenderService>() : nullptr)
                {
                    const auto rendererKey = rendererKeyFromRenderGraphUri(ctx.state.currentEditingRenderGraph);
                    if (renderService->reloadRenderPipeline(ctx.state.currentEditingRenderGraph, rendererKey))
                    {
                        status = "Switched render graph";
                        ctx.state.statusMessage =
                            "Renderer '" + rendererKey + "': " + ctx.state.currentEditingRenderGraph;
                    }
                    else
                    {
                        status                  = "Failed to switch render graph";
                        ctx.state.statusMessage = status;
                    }
                }
            }

            return changed;
        }

        std::vector<std::filesystem::path> listProjectFilesWithSuffix(const EditorContext& ctx, std::string_view suffix)
        {
            return collectProjectAssetFilesWithSuffix(ctx.state.currentProject, ctx.state.currentAssetRoot, suffix);
        }

        std::vector<std::string> listProjectFullscreenShaders(const EditorContext& ctx)
        {
            std::vector<std::string> out;
            if (ctx.state.currentProject.empty())
                return out;

            const auto root = (ctx.state.currentProject / ctx.state.currentAssetRoot / "shaders").lexically_normal();
            std::error_code ec;
            for (const auto& entry : std::filesystem::recursive_directory_iterator(root, ec))
            {
                if (ec)
                    break;
                if (!entry.is_regular_file(ec))
                    continue;
                const auto filename = entry.path().filename().generic_string();
                if (!filename.ends_with(".frag.vshader"))
                    continue;
                auto rel = std::filesystem::relative(entry.path(), root, ec).generic_string();
                if (ec)
                    rel = filename;
                if (rel.ends_with(".vshader"))
                    rel.resize(rel.size() - std::string_view(".vshader").size());
                out.push_back(rel);
            }
            std::sort(out.begin(), out.end());
            return out;
        }

        bool isImportedAssetPath(std::string_view logicalPath)
        {
            return logicalPath == "imported" || logicalPath.starts_with("imported/");
        }

        std::vector<std::filesystem::path> collectProjectLuaSourceFiles(const EditorContext& ctx)
        {
            std::vector<std::filesystem::path> files;
            if (ctx.state.currentProject.empty() || ctx.state.currentAssetRoot.empty())
                return files;

            const auto root = (ctx.state.currentProject / ctx.state.currentAssetRoot).lexically_normal();
            std::error_code ec;
            if (!std::filesystem::is_directory(root, ec))
                return files;

            for (const auto& entry : std::filesystem::recursive_directory_iterator(root, ec))
            {
                if (ec)
                    break;
                if (!entry.is_regular_file(ec) || entry.path().extension() != ".lua")
                    continue;
                std::error_code relEc;
                const auto      rel = std::filesystem::relative(entry.path(), root, relEc);
                if (relEc || rel.empty() || isImportedAssetPath(rel.generic_string()))
                    continue;
                files.push_back(entry.path().lexically_normal());
            }
            std::sort(files.begin(), files.end());
            return files;
        }

        std::optional<EditorProjectGraphPassDesc> loadRenderGraphPassLuaDesc(const std::filesystem::path& path)
        {
            std::ifstream file(path);
            if (!file.is_open())
                return std::nullopt;

            std::stringstream buffer;
            buffer << file.rdbuf();
            const auto text = buffer.str();
            if (text.find("RenderGraphPass") == std::string::npos)
                return std::nullopt;

            sol::state lua;
            lua.open_libraries(sol::lib::base, sol::lib::table, sol::lib::string, sol::lib::math);
            lua.set_function("RenderGraphPass", [](sol::table t) { return t; });
            auto result = lua.safe_script(text, &sol::script_pass_on_error);
            if (!result.valid())
                return std::nullopt;

            sol::object obj = result;
            if (!obj.is<sol::table>())
                return std::nullopt;

            sol::table  passTable = obj.as<sol::table>();
            EditorProjectGraphPassDesc desc;
            desc.type = solString(passTable, "type", solString(passTable, "name"));
            if (desc.type.empty())
                return std::nullopt;

            desc.inputs = solStringList(passTable, "inputs");
            if (desc.inputs.empty())
                desc.inputs.push_back(solString(passTable, "input", "source"));
            desc.outputs = solStringList(passTable, "outputs");
            if (desc.outputs.empty())
                desc.outputs.push_back(solString(passTable, "output", "color"));

            sol::object shaderObj = passTable["shader"];
            if (shaderObj.is<sol::table>())
            {
                sol::table  shaderTable = shaderObj.as<sol::table>();
                sol::object libraryObj  = shaderTable["library"];
                sol::object fragmentObj = shaderTable["fragment"];
                sol::object computeObj  = shaderTable["compute"];
                if (libraryObj.is<std::string>())
                    desc.shader.library = libraryObj.as<std::string>();
                if (fragmentObj.is<std::string>())
                    desc.shader.fragment = fragmentObj.as<std::string>();
                if (computeObj.is<std::string>())
                    desc.shader.compute = computeObj.as<std::string>();
                desc.hasShader = !desc.shader.fragment.empty() || !desc.shader.compute.empty();
            }

            return desc;
        }

        std::vector<std::string> listEditorProjectRenderGraphPassTypes(const EditorContext& ctx)
        {
            std::vector<std::string> types;
            for (const auto& path : collectProjectLuaSourceFiles(ctx))
            {
                const auto pass = loadRenderGraphPassLuaDesc(path);
                if (!pass)
                    continue;
                types.push_back(pass->type);
            }

            std::sort(types.begin(), types.end());
            types.erase(std::unique(types.begin(), types.end()), types.end());
            return types;
        }

        void registerEditorProjectRenderGraphPasses(const EditorContext&               ctx,
                                                    vrendergraph::RenderGraphRegistry& registry)
        {
            const EditorCpuScope perf {ctx, "Editor::RenderGraph/RegisterPassCatalog"};
            const auto noop = [](FrameGraph&,
                                 FrameGraphBlackboard&,
                                 const vrendergraph::ParamBlock&,
                                 vrendergraph::PassBuildContext&) {};
            for (const auto& path : collectProjectLuaSourceFiles(ctx))
            {
                const auto pass = loadRenderGraphPassLuaDesc(path);
                if (!pass)
                    continue;

                std::string type = pass->type;
                if (type.empty() || registry.contains(type))
                    continue;

                registry.registerPass(vrendergraph::PassDefinition {
                    .type    = type,
                    .setup   = noop,
                    .inputs  = pass->inputs,
                    .outputs = pass->outputs,
                    .params =
                        {
                            {.name = "name", .type = vrendergraph::ParamType::eString, .defaultValue = type},
                        },
                });
            }
        }

        std::optional<EditorShaderRef> builtinPassShaderRef(std::string_view type)
        {
            if (type == "DeferredLighting")
                return EditorShaderRef {
                    .library  = "builtin",
                    .profile  = vultra::rhi::ShaderProfile::eHighend,
                    .fragment = "deferred_lighting.frag",
                };
            if (type == "Ssao")
                return EditorShaderRef {
                    .library  = "builtin",
                    .profile  = vultra::rhi::ShaderProfile::eHighend,
                    .fragment = "ssao.frag",
                };
            if (type == "Ssr")
                return EditorShaderRef {
                    .library  = "builtin",
                    .profile  = vultra::rhi::ShaderProfile::eHighend,
                    .fragment = "ssr.frag",
                };
            if (type == "SelectionOutline")
                return EditorShaderRef {
                    .library  = "builtin",
                    .profile  = vultra::rhi::ShaderProfile::eHighend,
                    .fragment = "selection_outline.frag",
                };
            if (type == "ToneMapping")
                return EditorShaderRef {.library = "builtin", .fragment = "tone_mapping.frag"};
            return std::nullopt;
        }

        std::optional<EditorShaderRef> projectPassShaderRef(const EditorContext& ctx, std::string_view type)
        {
            static std::string                                                        cachedPrefix;
            static std::unordered_map<std::string, std::optional<EditorShaderRef>> cachedRefs;

            const auto prefix = projectCachePrefix(ctx);
            if (cachedPrefix != prefix)
            {
                cachedPrefix = prefix;
                cachedRefs.clear();
            }

            const std::string typeKey {type};
            if (const auto cached = cachedRefs.find(typeKey); cached != cachedRefs.end())
                return cached->second;

            const EditorCpuScope perf {ctx, "Editor::RenderGraph/ProjectPassShaderScan"};
            for (const auto& path : collectProjectLuaSourceFiles(ctx))
            {
                const auto pass = loadRenderGraphPassLuaDesc(path);
                if (!pass || pass->type != type)
                    continue;

                if (!pass->hasShader)
                    return std::nullopt;

                EditorShaderRef ref = pass->shader;
                if (!ref.fragment.empty() || !ref.compute.empty())
                {
                    cachedRefs[typeKey] = ref;
                    return ref;
                }
            }

            cachedRefs[typeKey] = std::nullopt;
            return std::nullopt;
        }

        std::optional<EditorShaderRef> resolvePassShaderRef(const EditorContext&          ctx,
                                                            const vrendergraph::PassDecl& pass)
        {
            const auto explicitLibrary = pass.params.get<std::string>("library", "project");
            const auto explicitFragment = pass.params.get<std::string>("fragment", {});
            const auto cacheKey = projectCachePrefix(ctx) + "|" + pass.type + "|" + explicitLibrary + "|" +
                                  explicitFragment;

            static std::unordered_map<std::string, std::optional<EditorShaderRef>> cachedRefs;
            if (const auto cached = cachedRefs.find(cacheKey); cached != cachedRefs.end())
                return cached->second;

            const EditorCpuScope perf {ctx, "Editor::RenderGraph/ResolvePassShader"};
            std::optional<EditorShaderRef> result;
            if (!explicitFragment.empty())
            {
                result = EditorShaderRef {
                    .library  = explicitLibrary,
                    .profile  = explicitLibrary == "builtin" ? vultra::rhi::ShaderProfile::eGeneral :
                                                                vultra::rhi::ShaderProfile::eUnspecified,
                    .fragment = explicitFragment,
                };
            }
            else if (auto ref = builtinPassShaderRef(pass.type))
                result = ref;
            else
                result = projectPassShaderRef(ctx, pass.type);

            cachedRefs[cacheKey] = result;
            return result;
        }

        std::string prettifyEnumLabel(std::string_view label)
        {
            std::string out;
            out.reserve(label.size() + 8);

            auto appendSpace = [&]() {
                if (!out.empty() && out.back() != ' ')
                    out.push_back(' ');
            };

            for (size_t i = 0; i < label.size(); ++i)
            {
                const unsigned char ch = static_cast<unsigned char>(label[i]);
                if (ch == '_' || ch == '-')
                {
                    appendSpace();
                    continue;
                }

                if (i > 0 && std::isupper(ch))
                {
                    const unsigned char prev = static_cast<unsigned char>(label[i - 1]);
                    const unsigned char next = i + 1 < label.size() ? static_cast<unsigned char>(label[i + 1]) : 0;
                    const bool          startsNewWord = std::islower(prev) || std::isdigit(prev);
                    const bool          endsAcronym   = std::isupper(prev) && next != 0 && std::islower(next);
                    if (startsNewWord || endsAcronym)
                        appendSpace();
                }

                out.push_back(static_cast<char>(ch));
            }

            return out;
        }

        std::vector<ParamEnumOption> readShaderSourceEnumOptions(const EditorContext&   ctx,
                                                                 const EditorShaderRef& shaderRef,
                                                                 std::string_view       paramName)
        {
            const auto shaderPath = findShaderSourceFile(ctx, shaderRef);
            if (!shaderPath)
                return {};

            std::ifstream file(*shaderPath);
            if (!file.is_open())
                return {};

            std::string line;
            while (std::getline(file, line))
            {
                auto       view    = std::string_view(line);
                const auto comment = view.find("//");
                if (comment != std::string_view::npos)
                    view = view.substr(0, comment);

                const auto strippedLine = trim(view);
                view                    = strippedLine;
                if (!view.starts_with(paramName))
                    continue;

                auto restString = trim(view.substr(paramName.size()));
                auto rest       = std::string_view(restString);
                if (!rest.starts_with(":"))
                    continue;
                restString = trim(rest.substr(1));
                rest       = restString;
                if (!rest.starts_with("enum("))
                    continue;

                const auto open  = rest.find('(');
                const auto close = rest.find(')', open + 1);
                if (open == std::string_view::npos || close == std::string_view::npos || close <= open)
                    continue;

                std::vector<ParamEnumOption> options;
                std::stringstream            ss {std::string(rest.substr(open + 1, close - open - 1))};
                std::string                  item;
                while (std::getline(ss, item, ','))
                {
                    item = trim(item);
                    if (item.empty())
                        continue;

                    const auto eq = item.rfind('=');
                    if (eq == std::string::npos)
                    {
                        options.push_back(ParamEnumOption {
                            .label = prettifyEnumLabel(item),
                            .value = static_cast<int>(options.size()),
                        });
                        continue;
                    }

                    auto label = trim(std::string_view(item).substr(0, eq));
                    auto value = trim(std::string_view(item).substr(eq + 1));
                    try
                    {
                        options.push_back(
                            ParamEnumOption {.label = prettifyEnumLabel(label), .value = std::stoi(value)});
                    }
                    catch (...)
                    {}
                }
                return options;
            }

            return {};
        }

        std::optional<vrendergraph::ParamDesc> readShaderSourceParamDesc(std::string_view line)
        {
            const auto colon = line.find(':');
            if (colon == std::string_view::npos)
                return std::nullopt;

            vrendergraph::ParamDesc desc {.name = trim(line.substr(0, colon))};
            auto                    restString = trim(line.substr(colon + 1));
            auto                    rest       = std::string_view(restString);
            if (desc.name.empty() || rest.empty())
                return std::nullopt;

            auto parseDefaultAndRange = [&](std::string_view afterType, auto parseValue) {
                const auto eq = afterType.find('=');
                if (eq != std::string_view::npos)
                {
                    auto       value    = trim(afterType.substr(eq + 1));
                    const auto rangePos = value.find("range(");
                    if (rangePos != std::string::npos)
                        value = trim(std::string_view(value).substr(0, rangePos));
                    desc.defaultValue = parseValue(value);
                }

                const auto range = afterType.find("range(");
                if (range != std::string_view::npos)
                {
                    const auto open  = afterType.find('(', range);
                    const auto comma = afterType.find(',', open + 1);
                    const auto close = afterType.find(')', comma + 1);
                    if (open != std::string_view::npos && comma != std::string_view::npos &&
                        close != std::string_view::npos)
                    {
                        desc.minValue = parseValue(trim(afterType.substr(open + 1, comma - open - 1)));
                        desc.maxValue = parseValue(trim(afterType.substr(comma + 1, close - comma - 1)));
                    }
                }
            };

            if (rest.starts_with("float"))
            {
                desc.type         = vrendergraph::ParamType::eFloat;
                desc.defaultValue = 0.0f;
                parseDefaultAndRange(rest.substr(5), [](std::string_view value) {
                    return nlohmann::json(std::stof(std::string(value)));
                });
                return desc;
            }
            if (rest.starts_with("int"))
            {
                desc.type         = vrendergraph::ParamType::eInt;
                desc.defaultValue = 0;
                parseDefaultAndRange(rest.substr(3), [](std::string_view value) {
                    return nlohmann::json(std::stoi(std::string(value)));
                });
                return desc;
            }
            if (rest.starts_with("bool"))
            {
                desc.type         = vrendergraph::ParamType::eBoolean;
                desc.defaultValue = false;
                parseDefaultAndRange(rest.substr(4), [](std::string_view value) {
                    return nlohmann::json(value == "true" || value == "1");
                });
                return desc;
            }
            if (rest.starts_with("enum("))
            {
                desc.type         = vrendergraph::ParamType::eInt;
                desc.defaultValue = 0;

                const auto close = rest.find(')');
                if (close == std::string_view::npos)
                    return desc;

                std::vector<ParamEnumOption> options;
                std::stringstream            ss {std::string(rest.substr(5, close - 5))};
                std::string                  item;
                while (std::getline(ss, item, ','))
                {
                    item = trim(item);
                    if (item.empty())
                        continue;
                    const auto eq = item.rfind('=');
                    if (eq == std::string::npos)
                    {
                        options.push_back({.label = item, .value = static_cast<int>(options.size())});
                    }
                    else
                    {
                        auto label = trim(std::string_view(item).substr(0, eq));
                        auto value = trim(std::string_view(item).substr(eq + 1));
                        try
                        {
                            options.push_back({.label = std::move(label), .value = std::stoi(value)});
                        }
                        catch (...)
                        {}
                    }
                }

                const auto eq = rest.find('=', close + 1);
                if (eq != std::string_view::npos)
                {
                    auto       defaultToken = trim(rest.substr(eq + 1));
                    const auto space        = defaultToken.find_first_of(" \t");
                    if (space != std::string::npos)
                        defaultToken.resize(space);
                    for (const auto& option : options)
                    {
                        if (option.label == defaultToken)
                        {
                            desc.defaultValue = option.value;
                            break;
                        }
                    }
                }
                return desc;
            }

            return std::nullopt;
        }

        std::vector<vrendergraph::ParamDesc> readShaderSourceParamDescs(const EditorContext&   ctx,
                                                                        const EditorShaderRef& shaderRef)
        {
            const auto shaderPath = findShaderSourceFile(ctx, shaderRef);
            if (!shaderPath)
                return {};

            std::ifstream file(*shaderPath);
            if (!file.is_open())
                return {};

            std::vector<vrendergraph::ParamDesc> params;
            bool                                 inProperties = false;
            std::string                          line;
            while (std::getline(file, line))
            {
                auto       view    = std::string_view(line);
                const auto comment = view.find("//");
                if (comment != std::string_view::npos)
                    view = view.substr(0, comment);
                const auto stripped = trim(view);
                if (stripped.empty())
                    continue;

                if (stripped.starts_with("["))
                {
                    inProperties = stripped == "[properties]";
                    continue;
                }
                if (!inProperties)
                    continue;

                if (auto desc = readShaderSourceParamDesc(stripped))
                    params.push_back(std::move(*desc));
            }
            return params;
        }

        std::vector<ParamEnumOption>
        readShaderParamEnum(const EditorContext& ctx, const vrendergraph::PassDecl& pass, std::string_view paramName)
        {
            const auto shaderRef = resolvePassShaderRef(ctx, pass);
            const auto cacheKey =
                projectCachePrefix(ctx) + "|" +
                (shaderRef && !primaryShaderId(*shaderRef).empty() ? shaderRefCacheKey(*shaderRef) : pass.type) + "|" +
                std::string(paramName);

            static std::unordered_map<std::string, std::vector<ParamEnumOption>> cachedEnums;
            if (const auto cached = cachedEnums.find(cacheKey); cached != cachedEnums.end())
                return cached->second;

            const EditorCpuScope perf {ctx, "Editor::RenderGraph/ShaderParamEnum"};
            auto result = [&]() -> std::vector<ParamEnumOption> {
                if (shaderRef && !primaryShaderId(*shaderRef).empty())
                {
                    if (auto sourceOptions = readShaderSourceEnumOptions(ctx, *shaderRef, paramName);
                        !sourceOptions.empty())
                        return sourceOptions;

                    auto* shaderService = ctx.services ? ctx.services->tryGet<vultra::IShaderService>() : nullptr;
                    vultra::rhi::ShaderLibraryRuntime* library = nullptr;
                    if (shaderService)
                    {
                        if (shaderRef->library == "builtin")
                            library = &shaderService->builtinLibrary(shaderRef->profile);
                        else
                        {
                            library = shaderService->findProjectLibrary("res://shaders/project.vshaderlib.lua");
                            if (!library)
                                library = shaderService->loadProjectLibrary("res://shaders/project.vshaderlib.lua");
                        }
                    }

                    auto loadEnumOptions = [&]() -> std::vector<ParamEnumOption> {
                        if (!library)
                            return {};

                        const auto shaderId = primaryShaderId(*shaderRef);
                        const auto stage    = primaryShaderStage(*shaderRef);
                        std::vector<std::string> shaderIds {shaderId};
                        if (stage == vshadersystem::ShaderStage::eFrag && shaderId.find('/') == std::string::npos &&
                            shaderId.find('\\') == std::string::npos)
                            shaderIds.push_back("fullscreen/" + shaderId);
                        if (stage == vshadersystem::ShaderStage::eComp && shaderId.find('/') == std::string::npos &&
                            shaderId.find('\\') == std::string::npos)
                            shaderIds.push_back("compute/" + shaderId);

                        for (const auto& shaderId : shaderIds)
                        {
                            const auto variantHash = vultra::rhi::ShaderLibraryRuntime::computeVariantHash(
                                shaderId, stage, {});
                            if (!library->hasVariant(variantHash, stage))
                                continue;
                            auto shader = library->load(variantHash, stage);
                            if (!shader)
                                continue;

                            for (const auto& reflectedParam : shader->materialDesc.params)
                            {
                                if (reflectedParam.name != paramName || reflectedParam.enumOptions.empty())
                                    continue;

                                std::vector<ParamEnumOption> options;
                                options.reserve(reflectedParam.enumOptions.size());
                                for (const auto& option : reflectedParam.enumOptions)
                                    options.push_back(ParamEnumOption {.label = option.label, .value = option.value});
                                return options;
                            }
                        }

                        return {};
                    };

                    auto reflectedOptions = loadEnumOptions();
                    if (!reflectedOptions.empty())
                    {
                        if (auto sourceOptions = readShaderSourceEnumOptions(ctx, *shaderRef, paramName);
                            !sourceOptions.empty() && sourceOptions.size() == reflectedOptions.size())
                            return sourceOptions;
                        return reflectedOptions;
                    }
                }

                const auto shaderPath =
                    shaderRef ? findShaderSourceFile(ctx, *shaderRef) : std::optional<std::filesystem::path> {};
                if (!shaderPath)
                    return {};

                std::ifstream file(*shaderPath);
                if (!file.is_open())
                    return {};

                const std::string prefix = "@param_enum " + std::string(paramName);
                std::string       line;
                while (std::getline(file, line))
                {
                    const auto marker = line.find(prefix);
                    if (marker == std::string::npos)
                        continue;

                    std::vector<ParamEnumOption> options;
                    std::string                  rest = trim(std::string_view(line).substr(marker + prefix.size()));
                    std::stringstream            ss(rest);
                    std::string                  item;
                    while (std::getline(ss, item, ','))
                    {
                        item          = trim(item);
                        const auto eq = item.rfind('=');
                        if (eq == std::string::npos)
                            continue;

                        auto label = trim(std::string_view(item).substr(0, eq));
                        auto value = trim(std::string_view(item).substr(eq + 1));
                        try
                        {
                            options.push_back(ParamEnumOption {.label = std::move(label), .value = std::stoi(value)});
                        }
                        catch (...)
                        {}
                    }
                    return options;
                }

                return {};
            }();
            cachedEnums[cacheKey] = result;
            return result;
        }

        template<typename T>
        T readShaderDefaultValue(const vshadersystem::ParamDefault& value)
        {
            T out {};
            std::memcpy(&out, value.valueBuffer, std::min(sizeof(T), sizeof(value.valueBuffer)));
            return out;
        }

        std::optional<vrendergraph::ParamDesc>
        shaderParamDescFromReflection(const vshadersystem::MaterialParamDesc& param)
        {
            vrendergraph::ParamDesc desc {.name = param.name};

            switch (param.type)
            {
                case vshadersystem::ParamType::eFloat:
                    desc.type         = vrendergraph::ParamType::eFloat;
                    desc.defaultValue = param.hasDefault ?
                                            nlohmann::json(readShaderDefaultValue<float>(param.defaultValue)) :
                                            nlohmann::json(0.0f);
                    if (param.hasRange)
                    {
                        desc.minValue = static_cast<float>(param.range.min);
                        desc.maxValue = static_cast<float>(param.range.max);
                    }
                    break;
                case vshadersystem::ParamType::eInt:
                    desc.type         = vrendergraph::ParamType::eInt;
                    desc.defaultValue = param.hasDefault ?
                                            nlohmann::json(readShaderDefaultValue<int32_t>(param.defaultValue)) :
                                            nlohmann::json(0);
                    if (param.hasRange)
                    {
                        desc.minValue = static_cast<int>(param.range.min);
                        desc.maxValue = static_cast<int>(param.range.max);
                    }
                    break;
                case vshadersystem::ParamType::eUInt:
                    desc.type = vrendergraph::ParamType::eInt;
                    desc.defaultValue =
                        param.hasDefault ?
                            nlohmann::json(static_cast<int>(readShaderDefaultValue<uint32_t>(param.defaultValue))) :
                            nlohmann::json(0);
                    if (param.hasRange)
                    {
                        desc.minValue = static_cast<int>(param.range.min);
                        desc.maxValue = static_cast<int>(param.range.max);
                    }
                    break;
                case vshadersystem::ParamType::eBool:
                    desc.type         = vrendergraph::ParamType::eBoolean;
                    desc.defaultValue = param.hasDefault ?
                                            nlohmann::json(readShaderDefaultValue<bool>(param.defaultValue)) :
                                            nlohmann::json(false);
                    break;
                default:
                    return std::nullopt;
            }

            return desc;
        }

        std::vector<vrendergraph::ParamDesc> readShaderReflectedParamDescs(const EditorContext&          ctx,
                                                                           const vrendergraph::PassDecl& pass)
        {
            const auto shaderRef = resolvePassShaderRef(ctx, pass);
            if (!shaderRef || primaryShaderId(*shaderRef).empty())
                return {};

            if (auto sourceParams = readShaderSourceParamDescs(ctx, *shaderRef); !sourceParams.empty())
                return sourceParams;

            auto* shaderService = ctx.services ? ctx.services->tryGet<vultra::IShaderService>() : nullptr;
            if (!shaderService)
                return {};

            vultra::rhi::ShaderLibraryRuntime* library = nullptr;
            if (shaderRef->library == "builtin")
                library = &shaderService->builtinLibrary(shaderRef->profile);
            else
            {
                library = shaderService->findProjectLibrary("res://shaders/project.vshaderlib.lua");
                if (!library)
                    library = shaderService->loadProjectLibrary("res://shaders/project.vshaderlib.lua");
            }
            if (!library)
                return {};

            const auto shaderId = primaryShaderId(*shaderRef);
            const auto stage    = primaryShaderStage(*shaderRef);
            std::vector<std::string> shaderIds {shaderId};
            if (stage == vshadersystem::ShaderStage::eFrag && shaderId.find('/') == std::string::npos &&
                shaderId.find('\\') == std::string::npos)
                shaderIds.push_back("fullscreen/" + shaderId);
            if (stage == vshadersystem::ShaderStage::eComp && shaderId.find('/') == std::string::npos &&
                shaderId.find('\\') == std::string::npos)
                shaderIds.push_back("compute/" + shaderId);

            auto loadReflectedShader = [&](vultra::rhi::ShaderLibraryRuntime& shaderLibrary)
                -> std::optional<vultra::rhi::ShaderLibraryRuntime::LoadedShader> {
                for (const auto& shaderId : shaderIds)
                {
                    const auto variantHash = vultra::rhi::ShaderLibraryRuntime::computeVariantHash(
                        shaderId, stage, {});
                    if (!shaderLibrary.hasVariant(variantHash, stage))
                        continue;
                    auto shader = shaderLibrary.load(variantHash, stage);
                    if (shader)
                        return shader;
                }
                return std::nullopt;
            };

            auto shader = loadReflectedShader(*library);
            if (!shader || shader->materialDesc.params.empty())
                return {};

            std::vector<vrendergraph::ParamDesc> params;
            params.reserve(shader->materialDesc.params.size());
            for (const auto& reflectedParam : shader->materialDesc.params)
            {
                if (auto desc = shaderParamDescFromReflection(reflectedParam))
                    params.push_back(std::move(*desc));
            }
            return params;
        }

        std::vector<vrendergraph::ParamDesc> readShaderParamDescs(const EditorContext&          ctx,
                                                                  const vrendergraph::PassDecl& pass)
        {
            const auto shaderRef = resolvePassShaderRef(ctx, pass);
            const auto cacheKey =
                projectCachePrefix(ctx) + "|" +
                (shaderRef && !primaryShaderId(*shaderRef).empty() ? shaderRefCacheKey(*shaderRef) : pass.type);

            static std::unordered_map<std::string, std::vector<vrendergraph::ParamDesc>> cachedParams;
            if (const auto cached = cachedParams.find(cacheKey); cached != cachedParams.end())
                return cached->second;

            const EditorCpuScope perf {ctx, "Editor::RenderGraph/ShaderParamDescs"};
            auto params            = readShaderReflectedParamDescs(ctx, pass);
            cachedParams[cacheKey] = params;
            return params;
        }

        glm::mat4 makeTransformMatrix(const vultra::TransformComponent& transform)
        {
            return glm::translate(glm::mat4 {1.0f}, transform.position) * glm::mat4_cast(transform.rotation) *
                   glm::scale(glm::mat4 {1.0f}, transform.scale);
        }

        glm::mat4 makeWorldTransformMatrix(const entt::registry& reg, const entt::entity entity)
        {
            const auto* transform = reg.try_get<vultra::TransformComponent>(entity);
            if (!transform)
                return glm::mat4 {1.0f};

            const auto  local     = makeTransformMatrix(*transform);
            const auto* hierarchy = reg.try_get<vultra::HierarchyComponent>(entity);
            if (!hierarchy || hierarchy->parent == entt::null || !reg.valid(hierarchy->parent))
                return local;

            return makeWorldTransformMatrix(reg, hierarchy->parent) * local;
        }

        glm::mat4 makeGameProjection(const vultra::CameraComponent& camera, const float aspect)
        {
            const float zNear = std::max(camera.zNear, 0.0001f);
            const float zFar  = std::max(camera.zFar, zNear + 0.0001f);
            if (camera.projection == 1u)
            {
                const float height = std::max(camera.orthographicHeight, 0.0001f);
                const float width  = height * std::max(aspect, 0.0001f);
                return glm::orthoRH_ZO(-width * 0.5f, width * 0.5f, -height * 0.5f, height * 0.5f, zNear, zFar);
            }

            return glm::perspectiveRH_ZO(glm::radians(camera.fovYDegrees), std::max(aspect, 0.0001f), zNear, zFar);
        }

        entt::entity findPrimaryCamera(vultra::World& world)
        {
            auto& reg  = world.registry();
            auto  view = reg.view<vultra::IDComponent, vultra::TransformComponent, vultra::CameraComponent>();

            entt::entity best         = entt::null;
            int          bestPriority = std::numeric_limits<int>::min();
            for (auto e : view)
            {
                const auto& camera = view.get<vultra::CameraComponent>(e);
                if (!camera.primary)
                    continue;
                if (best == entt::null || camera.priority >= bestPriority)
                {
                    best         = e;
                    bestPriority = camera.priority;
                }
            }
            return best;
        }

        vultra::RenderCamera makeRenderGraphCamera(vultra::World&        world,
                                                   const entt::entity    entity,
                                                   const float           aspect,
                                                   vultra::rhi::Texture* target,
                                                   std::string           name,
                                                   std::string           rendererKey)
        {
            auto& reg    = world.registry();
            auto& id     = reg.get<vultra::IDComponent>(entity);
            auto& camera = reg.get<vultra::CameraComponent>(entity);

            vultra::RenderCamera out {};
            out.uuid                    = id.uuid;
            out.name                    = std::move(name);
            out.priority                = camera.priority;
            out.view                    = glm::inverse(makeWorldTransformMatrix(reg, entity));
            out.projection              = makeGameProjection(camera, aspect);
            out.zNear                   = std::max(camera.zNear, 0.0001f);
            out.zFar                    = std::max(camera.zFar, out.zNear + 0.0001f);
            out.fovY                    = glm::radians(camera.fovYDegrees);
            out.target                  = target;
            out.clearValue              = camera.clearColor;
            out.clearValue.a            = 1.0f;
            out.clearMode               = camera.clearMode;
            out.renderImGui             = false;
            out.debugEntityIdOutput     = false;
            out.selectionOutlineEnabled = false;
            out.rendererKey = rendererKey.empty() ? (camera.rendererKey.empty() ? "universal" : camera.rendererKey) :
                                                    std::move(rendererKey);
            return out;
        }

        vultra::RenderCamera makeRenderGraphPreviewCamera(vultra::World&        world,
                                                          const entt::entity    entity,
                                                          const float           aspect,
                                                          vultra::rhi::Texture* target,
                                                          std::string           rendererKey)
        {
            auto out = makeRenderGraphCamera(world, entity, aspect, target, "Render Graph Preview", rendererKey);
            std::transform(rendererKey.begin(), rendererKey.end(), rendererKey.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            if (rendererKey.find("xr") != std::string::npos || rendererKey.find("stereo") != std::string::npos)
            {
                out.isXRView        = true;
                out.isXRPrimaryView = true;
                out.viewCount       = 2u;
                out.xrViewEnabled   = true;
                out.xrFallbackMono  = false;
            }
            return out;
        }

        std::string normalizedTextureLookupKey(std::string_view key)
        {
            key = trim(key);
            if (key.empty())
                return {};

            if (const auto at = key.find('@'); at != std::string_view::npos)
                key = key.substr(0, at);

            while (!key.empty() && std::isspace(static_cast<unsigned char>(key.front())) != 0)
                key.remove_prefix(1);
            while (!key.empty() && std::isspace(static_cast<unsigned char>(key.back())) != 0)
                key.remove_suffix(1);

            return std::string {key};
        }

        std::string lower(std::string_view text)
        {
            std::string out(text);
            std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return out;
        }

        bool containsIgnoreCase(std::string_view haystack, std::string_view needle)
        {
            if (needle.empty())
                return true;
            return lower(haystack).find(lower(needle)) != std::string::npos;
        }

        bool isXrRenderGraphUri(std::string_view uri)
        {
            return containsIgnoreCase(uri, "xr") || containsIgnoreCase(uri, "stereo");
        }

        void addTextureLookupKey(std::unordered_map<std::string, const vultra::FrameGraphDebugTexture*>& textureByName,
                                 std::string_view                                                        key,
                                 const vultra::FrameGraphDebugTexture&                                   texture,
                                 const bool replace = false)
        {
            const auto normalized = normalizedTextureLookupKey(key);
            if (normalized.empty())
                return;

            if (replace)
                textureByName[normalized] = &texture;
            else
                textureByName.try_emplace(normalized, &texture);
        }

        void addTextureLookupKeyWithoutLayer(
            std::unordered_map<std::string, const vultra::FrameGraphDebugTexture*>& textureByName,
            std::string_view                                                        key,
            const vultra::FrameGraphDebugTexture&                                   texture,
            const bool                                                              replace = false)
        {
            addTextureLookupKey(textureByName, key, texture, replace);
            if (const auto layerPos = key.find("/layer:"); layerPos != std::string_view::npos)
                addTextureLookupKey(textureByName, key.substr(0, layerPos), texture, replace);
        }

        std::string textureKeyWithoutLayer(std::string_view key)
        {
            if (const auto layerPos = key.find("/layer:"); layerPos != std::string_view::npos)
                key = key.substr(0, layerPos);
            return std::string {key};
        }

        struct RuntimeStereoTexturePair
        {
            const vultra::FrameGraphDebugTexture* left {nullptr};
            const vultra::FrameGraphDebugTexture* right {nullptr};
        };

        RuntimeStereoTexturePair findRuntimeStereoTexturePair(
            const std::vector<vultra::FrameGraphDebugTexture>& textures,
            const vultra::FrameGraphDebugTexture&              selected)
        {
            RuntimeStereoTexturePair pair;
            const auto               selectedBaseKey = textureKeyWithoutLayer(selected.resourceKey);
            for (const auto& texture : textures)
            {
                if (texture.camera != selected.camera || texture.renderer != selected.renderer ||
                    textureKeyWithoutLayer(texture.resourceKey) != selectedBaseKey)
                {
                    continue;
                }
                if (texture.layer == 0u)
                    pair.left = &texture;
                else if (texture.layer == 1u)
                    pair.right = &texture;
            }
            return pair;
        }

        std::string runtimeGraphDisplayLabel(std::string_view label)
        {
            label                                         = trim(label);
            constexpr std::string_view debugCapturePrefix = "DebugCapture/";
            if (label.starts_with(debugCapturePrefix))
                label.remove_prefix(debugCapturePrefix.size());

            constexpr std::string_view renderGraphPreviewPrefix = "Render Graph Preview/";
            if (label.starts_with(renderGraphPreviewPrefix))
                label.remove_prefix(renderGraphPreviewPrefix.size());

            return std::string {label};
        }

        bool endsWithRuntimeVersionSuffix(std::string_view label, const int version)
        {
            if (version <= 1)
                return true;

            const auto suffix = " v" + std::to_string(version);
            return label.size() >= suffix.size() && label.substr(label.size() - suffix.size()) == suffix;
        }

        std::string runtimeResourceLabelWithVersion(std::string label, const int version)
        {
            if (version > 1 && !endsWithRuntimeVersionSuffix(label, version))
                label += " v" + std::to_string(version);
            return label;
        }

        std::string runtimeGraphNodeDisplayLabel(std::string label, std::string_view kind, const int version)
        {
            if (kind == "resource")
                return runtimeResourceLabelWithVersion(std::move(label), version);
            return label;
        }

        bool isDebugCaptureRuntimeGraphNode(std::string_view id, std::string_view label)
        {
            id    = trim(id);
            label = trim(label);
            return id.starts_with("pass:DebugCapture/") || id.starts_with("resource:DebugCapture/") ||
                   label.starts_with("DebugCapture/");
        }

        bool isAnonymousRuntimeGraphDebugId(std::string_view id, std::string_view label)
        {
            id    = trim(id);
            label = trim(label);
            if (!label.empty() && label != id)
                return false;

            auto isNumericSuffix = [](std::string_view text, std::string_view prefix) {
                if (!text.starts_with(prefix) || text.size() == prefix.size())
                    return false;
                text.remove_prefix(prefix.size());
                return std::all_of(text.begin(), text.end(), [](const char c) {
                    return std::isdigit(static_cast<unsigned char>(c)) != 0;
                });
            };
            return isNumericSuffix(id, "resource:") || isNumericSuffix(id, "pass:");
        }

        bool isHiddenRuntimeGraphDebugNode(std::string_view id, std::string_view label)
        {
            return isDebugCaptureRuntimeGraphNode(id, label);
        }

        std::string dotEscape(std::string_view text)
        {
            std::string out;
            out.reserve(text.size() + 8);
            for (const char c : text)
            {
                switch (c)
                {
                    case '\\':
                        out += "\\\\";
                        break;
                    case '"':
                        out += "\\\"";
                        break;
                    case '\n':
                        out += "\\n";
                        break;
                    case '\r':
                        break;
                    default:
                        out += c;
                        break;
                }
            }
            return out;
        }

        std::string sanitizeRuntimeGraphDotForLayout(std::string dot)
        {
            auto replaceAll = [&](const std::string_view needle, const std::string_view replacement) {
                size_t pos = 0;
                while ((pos = dot.find(needle, pos)) != std::string::npos)
                {
                    dot.replace(pos, needle.size(), replacement);
                    pos += replacement.size();
                }
            };

            // The vrendergraph DOT fixes node boxes for compact output. Once HTML labels are enabled,
            // Graphviz can compute the real table label size, so let it grow nodes instead of warning.
            replaceAll("fixedsize=true", "fixedsize=false");
            replaceAll("fixedsize=\"true\"", "fixedsize=\"false\"");
            return dot;
        }

    } // namespace

    struct RenderGraphWindow::RuntimeGraphState
    {
        struct Edge
        {
            std::string from;
            std::string to;
            std::string label;
        };
        struct Node
        {
            std::string id;
            std::string label;
            std::string kind;
            bool        imported {false};
            bool        active {true};
            bool        sideEffect {false};
            int         version {0};
        };

        std::vector<Node>        nodes;
        std::vector<Edge>        edges;
        std::vector<std::string> graphLabels;
        std::vector<std::string> graphKeys;
        std::string              selectedGraphKey;
        int                      selectedGraphIndex {-1};
        std::string              camera;
        std::string              cameraDisplay;
        std::string              textureCamera;
        std::string              renderer;
        std::string              rawDot;
        size_t                   snapshotHash {0};

        bool isXrRelated() const
        {
            if (containsIgnoreCase(renderer, "xr") || containsIgnoreCase(renderer, "stereo"))
                return true;
            for (const auto& node : nodes)
            {
                if (containsIgnoreCase(node.label, "xr") || containsIgnoreCase(node.label, "stereo") ||
                    containsIgnoreCase(node.label, "left eye") || containsIgnoreCase(node.label, "right eye"))
                {
                    return true;
                }
            }
            return false;
        }

        void parse(std::string_view snapshot)
        {
            const size_t nextHash = std::hash<std::string_view> {}(snapshot);
            if (nextHash == snapshotHash)
                return;

            snapshotHash = nextHash;
            nodes.clear();
            edges.clear();
            graphLabels.clear();
            graphKeys.clear();
            camera.clear();
            cameraDisplay.clear();
            textureCamera.clear();
            renderer.clear();
            rawDot.clear();

            std::unordered_map<std::string, Node> nodeById;
            auto addNode = [&](std::string id, std::string label = {}, std::string kind = {}) {
                id    = trim(id);
                label = trim(label.empty() ? id : label);
                if (id.empty() || id == "node" || id == "edge" || id == "graph")
                    return;
                if (auto it = nodeById.find(id); it != nodeById.end())
                {
                    if (it->second.label == it->second.id && label != id)
                        it->second.label = std::move(label);
                    if (it->second.kind.empty() && !kind.empty())
                        it->second.kind = std::move(kind);
                    return;
                }
                const auto key = id;
                nodeById.emplace(key, Node {std::move(id), std::move(label), std::move(kind)});
            };

            std::vector<nlohmann::json> allGraphEntries;
            size_t                      lineStart = 0;
            while (lineStart < snapshot.size())
            {
                const size_t lineEnd = snapshot.find('\n', lineStart);
                const auto   line    = snapshot.substr(
                    lineStart, lineEnd == std::string_view::npos ? std::string_view::npos : lineEnd - lineStart);
                const auto trimmedLine = trim(line);
                if (!trimmedLine.empty())
                {
                    try
                    {
                        const auto json = nlohmann::json::parse(trimmedLine);
                        allGraphEntries.push_back(json);
                    }
                    catch (const std::exception&)
                    {}
                }

                if (lineEnd == std::string_view::npos)
                    break;
                lineStart = lineEnd + 1;
            }

            auto graphEntries = std::move(allGraphEntries);

            int    previewGraphIndex = -1;
            int    gameGraphIndex    = -1;
            int    cameraGraphIndex  = -1;
            int    largestGraphIndex = -1;
            size_t largestGraphSize  = 0;
            for (size_t i = 0; i < graphEntries.size(); ++i)
            {
                const auto& json        = graphEntries[i];
                const auto  rendererKey = json.value("renderer", std::string {"renderer?"});
                const auto  cameraName  = json.value("camera", std::string {"camera?"});
                const auto  nodesJson   = json.value("nodes", nlohmann::json::array());
                const auto  nodeCount   = nodesJson.size();
                auto        graphKey    = rendererKey + "/" + cameraName;
                if (std::find(graphKeys.begin(), graphKeys.end(), graphKey) != graphKeys.end())
                    graphKey += "#" + std::to_string(i);

                graphKeys.push_back(graphKey);
                graphLabels.push_back(runtimeCameraDisplayName(cameraName) + " - " + rendererKey + " (" +
                                      std::to_string(nodeCount) + " nodes)");

                if (previewGraphIndex < 0 && cameraName == "Render Graph Preview")
                    previewGraphIndex = static_cast<int>(i);
                if (gameGraphIndex < 0 &&
                    (cameraName.find("Game") != std::string::npos || cameraName.find("game") != std::string::npos))
                    gameGraphIndex = static_cast<int>(i);
                if (cameraGraphIndex < 0 && cameraName == "Camera")
                    cameraGraphIndex = static_cast<int>(i);
                if (nodeCount > largestGraphSize)
                {
                    largestGraphSize  = nodeCount;
                    largestGraphIndex = static_cast<int>(i);
                }
            }

            const int preferredGraphIndex =
                gameGraphIndex >= 0 ? gameGraphIndex : cameraGraphIndex >= 0 ? cameraGraphIndex : largestGraphIndex;
            const auto selectedIt = std::find(graphKeys.begin(), graphKeys.end(), selectedGraphKey);
            const int selectedIndex = selectedIt == graphKeys.end() ? -1 :
                                                                 static_cast<int>(std::distance(graphKeys.begin(), selectedIt));
            const bool selectedPreview =
                selectedIndex >= 0 &&
                graphEntries[static_cast<size_t>(selectedIndex)].value("camera", std::string {}) ==
                    "Render Graph Preview";
            if (selectedIndex >= 0 && !(selectedPreview && preferredGraphIndex >= 0))
            {
                selectedGraphIndex = selectedIndex;
            }
            else if (preferredGraphIndex >= 0)
            {
                selectedGraphIndex = preferredGraphIndex;
                selectedGraphKey   = graphKeys[static_cast<size_t>(selectedGraphIndex)];
            }
            else if (previewGraphIndex >= 0)
            {
                selectedGraphIndex = previewGraphIndex;
                selectedGraphKey   = graphKeys[static_cast<size_t>(selectedGraphIndex)];
            }
            else if (!graphLabels.empty())
            {
                selectedGraphIndex = 0;
                selectedGraphKey   = graphKeys.front();
            }

            std::vector<std::string>        dotNodeOrder;
            std::unordered_set<std::string> dotVisibleNodeIds;
            if (!graphEntries.empty() && selectedGraphIndex >= 0 &&
                selectedGraphIndex < static_cast<int>(graphEntries.size()))
            {
                const auto& selectedGraph = graphEntries[static_cast<size_t>(selectedGraphIndex)];
                camera                    = selectedGraph.value("camera", std::string {});
                cameraDisplay             = runtimeCameraDisplayName(camera);
                renderer                  = selectedGraph.value("renderer", std::string {});
                rawDot                    = sanitizeRuntimeGraphDotForLayout(selectedGraph.value("dot", std::string {}));
                textureCamera             = camera;
                const auto selectedNodesJson = selectedGraph.value("nodes", nlohmann::json::array());
                const auto selectedEdgesJson = selectedGraph.value("edges", nlohmann::json::array());
                std::unordered_set<std::string> debugCaptureNodeIds;

                for (const auto& node : selectedNodesJson)
                {
                    const auto id = node.value("id", std::string {});
                    if (isHiddenRuntimeGraphDebugNode(id, node.value("label", std::string {})))
                    {
                        if (!id.empty())
                            debugCaptureNodeIds.insert(id);
                        continue;
                    }
                    addNode(id, node.value("label", std::string {}), node.value("kind", std::string {}));
                    if (auto it = nodeById.find(id); it != nodeById.end())
                    {
                        it->second.imported   = node.value("imported", false);
                        it->second.active     = node.value("active", true);
                        it->second.sideEffect = node.value("sideEffect", false);
                        it->second.version    = node.value("version", 0);
                    }
                }
                for (const auto& edge : selectedEdgesJson)
                {
                    const auto label = edge.value("label", std::string {});
                    if (edge.contains("from") && edge.contains("to"))
                    {
                        const auto from = edge.value("from", std::string {});
                        const auto to   = edge.value("to", std::string {});
                        if (isHiddenRuntimeGraphDebugNode(from, {}) || isHiddenRuntimeGraphDebugNode(to, {}) ||
                            debugCaptureNodeIds.contains(from) || debugCaptureNodeIds.contains(to))
                            continue;
                        if (from.empty() || to.empty() || from == to)
                            continue;
                        if (!nodeById.contains(from))
                            addNode(from);
                        if (!nodeById.contains(to))
                            addNode(to);
                        edges.push_back({from, to, label});
                    }
                    else if (edge.contains("vertices") && edge["vertices"].is_array())
                    {
                        const auto& vertices = edge["vertices"];
                        for (size_t i = 1; i < vertices.size(); ++i)
                        {
                            const auto from = vertices[i - 1].get<std::string>();
                            const auto to   = vertices[i].get<std::string>();
                            if (isHiddenRuntimeGraphDebugNode(from, {}) || isHiddenRuntimeGraphDebugNode(to, {}) ||
                                debugCaptureNodeIds.contains(from) || debugCaptureNodeIds.contains(to))
                                continue;
                            if (from.empty() || to.empty() || from == to)
                                continue;
                            if (!nodeById.contains(from))
                                addNode(from);
                            if (!nodeById.contains(to))
                                addNode(to);
                            edges.push_back({from, to, label});
                        }
                    }
                }

                if (!rawDot.empty())
                {
                    struct DotEdge
                    {
                        std::string from;
                        std::string to;
                        std::string label;
                    };

                    std::vector<DotEdge>            dotEdges;
                    std::unordered_set<std::string> dotNodeIds;

                    auto unescapeDotString = [](std::string_view text) {
                        std::string out;
                        out.reserve(text.size());
                        bool escaped = false;
                        for (const char c : text)
                        {
                            if (escaped)
                            {
                                switch (c)
                                {
                                    case 'n':
                                        out.push_back('\n');
                                        break;
                                    case 'r':
                                        break;
                                    default:
                                        out.push_back(c);
                                        break;
                                }
                                escaped = false;
                                continue;
                            }
                            if (c == '\\')
                            {
                                escaped = true;
                                continue;
                            }
                            out.push_back(c);
                        }
                        return out;
                    };

                    auto stripDotHtmlLabel = [&](std::string_view text) {
                        std::string out;
                        out.reserve(text.size());
                        bool inTag = false;
                        for (size_t i = 0; i < text.size(); ++i)
                        {
                            const char c = text[i];
                            if (c == '<')
                            {
                                if (text.substr(i, 5) == "<BR/>" || text.substr(i, 4) == "<BR>")
                                    break;
                                inTag = true;
                                continue;
                            }
                            if (c == '>')
                            {
                                inTag = false;
                                continue;
                            }
                            if (inTag || c == '{' || c == '}')
                                continue;
                            if (c == '&')
                            {
                                const auto semi = text.find(';', i);
                                if (semi != std::string_view::npos)
                                {
                                    const auto entity = text.substr(i, semi - i + 1);
                                    if (entity == "&#x2605;" || entity == "&starf;")
                                        out.push_back('*');
                                    i = semi;
                                    continue;
                                }
                            }
                            out.push_back(c);
                        }
                        return trim(out);
                    };

                    auto hasAsciiIdentifierText = [](std::string_view text) {
                        return std::any_of(text.begin(), text.end(), [](const char c) {
                            return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == ':' ||
                                   c == '/' || c == '.';
                        });
                    };

                    auto readHtmlLabelTitle = [&](std::string_view line) {
                        const auto labelPos = line.find("label=<");
                        if (labelPos == std::string_view::npos)
                            return std::string {};
                        const auto begin = line.find('<', labelPos + 6);
                        const auto end   = line.find("|", begin == std::string_view::npos ? labelPos + 6 : begin);
                        if (begin == std::string_view::npos || end == std::string_view::npos || end <= begin)
                            return std::string {};
                        return stripDotHtmlLabel(line.substr(begin, end - begin));
                    };

                    auto readQuoted = [&](std::string_view line, size_t& pos) -> std::string {
                        pos = line.find('"', pos);
                        if (pos == std::string_view::npos)
                            return {};
                        ++pos;
                        std::string out;
                        bool        escaped = false;
                        for (; pos < line.size(); ++pos)
                        {
                            const char c = line[pos];
                            if (escaped)
                            {
                                out.push_back('\\');
                                out.push_back(c);
                                escaped = false;
                                continue;
                            }
                            if (c == '\\')
                            {
                                escaped = true;
                                continue;
                            }
                            if (c == '"')
                            {
                                ++pos;
                                break;
                            }
                            out.push_back(c);
                        }
                        return unescapeDotString(out);
                    };

                    auto readDotId = [](std::string_view line, size_t& pos) -> std::string {
                        while (pos < line.size() && (std::isspace(static_cast<unsigned char>(line[pos])) ||
                                                     line[pos] == '{' || line[pos] == ';' || line[pos] == ','))
                            ++pos;
                        if (pos >= line.size())
                            return {};
                        if (line[pos] == '"')
                            return {};
                        const size_t begin = pos;
                        while (pos < line.size() &&
                               (std::isalnum(static_cast<unsigned char>(line[pos])) || line[pos] == '_' ||
                                line[pos] == ':' || line[pos] == '.' || line[pos] == '-'))
                            ++pos;
                        return std::string {line.substr(begin, pos - begin)};
                    };

                    auto readAttribute = [&](std::string_view line, std::string_view name) -> std::string {
                        size_t search = 0;
                        while (search < line.size())
                        {
                            const auto found = line.find(name, search);
                            if (found == std::string_view::npos)
                                return {};
                            const bool leftOk =
                                found == 0 ||
                                (!std::isalnum(static_cast<unsigned char>(line[found - 1])) && line[found - 1] != '_');
                            const size_t afterName = found + name.size();
                            size_t       p         = afterName;
                            while (p < line.size() && std::isspace(static_cast<unsigned char>(line[p])))
                                ++p;
                            if (leftOk && p < line.size() && line[p] == '=')
                            {
                                ++p;
                                while (p < line.size() && std::isspace(static_cast<unsigned char>(line[p])))
                                    ++p;
                                if (p < line.size() && line[p] == '"')
                                    return readQuoted(line, p);
                                const size_t begin = p;
                                while (p < line.size() && line[p] != ',' && line[p] != ']')
                                    ++p;
                                return std::string {trim(line.substr(begin, p - begin))};
                            }
                            search = afterName;
                        }
                        return {};
                    };

                    auto dotKind = [](std::string_view id) -> std::string {
                        if (id.starts_with("pass:") ||
                            (id.size() >= 2 && id[0] == 'P' && std::isdigit(static_cast<unsigned char>(id[1]))))
                            return "pass";
                        if (id.starts_with("resource:") ||
                            (id.size() >= 2 && id[0] == 'R' && std::isdigit(static_cast<unsigned char>(id[1]))))
                            return "resource";
                        return {};
                    };

                    auto isDotRuntimeNodeId = [](std::string_view id) {
                        if (id.starts_with("pass:") || id.starts_with("resource:"))
                            return true;
                        return id.size() >= 2 && (id[0] == 'P' || id[0] == 'R') &&
                               std::isdigit(static_cast<unsigned char>(id[1]));
                    };

                    auto jsonNodeKeyForDotId = [](std::string_view id) -> std::string {
                        if (id.starts_with("pass:") || id.starts_with("resource:"))
                            return std::string {id};
                        if (id.size() < 2 || (id[0] != 'P' && id[0] != 'R') ||
                            std::isdigit(static_cast<unsigned char>(id[1])) == 0)
                        {
                            return {};
                        }

                        size_t end = 1;
                        while (end < id.size() && std::isdigit(static_cast<unsigned char>(id[end])) != 0)
                            ++end;
                        const auto prefix = id[0] == 'P' ? std::string_view {"pass:"} : std::string_view {"resource:"};
                        std::string key = std::string {prefix} + std::string {id.substr(1, end - 1)};
                        if (id[0] == 'R' && end + 1 < id.size() && id[end] == '_' &&
                            std::isdigit(static_cast<unsigned char>(id[end + 1])) != 0)
                        {
                            key += "_v";
                            key += std::string {id.substr(end + 1)};
                        }
                        return key;
                    };

                    auto addDotNode = [&](std::string id, std::string label, const std::string& fillColor) {
                        if (id.empty() || !isDotRuntimeNodeId(id) || dotNodeIds.contains(id))
                            return;
                        auto       kind    = dotKind(id);
                        const auto jsonKey = jsonNodeKeyForDotId(id);
                        if (auto jsonIt = nodeById.find(jsonKey); jsonIt != nodeById.end())
                        {
                            auto dotNode = jsonIt->second;
                            dotNode.id   = id;
                            nodeById[id] = std::move(dotNode);
                        }
                        else
                        {
                            if (label.empty() || !hasAsciiIdentifierText(label))
                                label = id;
                            addNode(id, label, kind);
                            if (auto it = nodeById.find(id); it != nodeById.end())
                            {
                                it->second.imported   = fillColor == "lightsteelblue";
                                it->second.sideEffect = fillColor == "orange";
                            }
                        }
                        dotNodeIds.insert(id);
                        dotVisibleNodeIds.insert(id);
                        dotNodeOrder.push_back(std::move(id));
                    };

                    size_t dotLineStart = 0;
                    while (dotLineStart < rawDot.size())
                    {
                        const size_t dotLineEnd = rawDot.find('\n', dotLineStart);
                        auto         line =
                            trim(std::string_view {rawDot.data() + dotLineStart,
                                                   dotLineEnd == std::string::npos ? rawDot.size() - dotLineStart :
                                                                                     dotLineEnd - dotLineStart});

                        if (!line.empty() && line.front() == '"')
                        {
                            size_t     pos   = 0;
                            auto       first = readQuoted(line, pos);
                            const auto arrow = line.find("->", pos);
                            if (arrow != std::string_view::npos)
                            {
                                pos         = arrow + 2;
                                auto second = readQuoted(line, pos);
                                if (!first.empty() && !second.empty() && first != second &&
                                    !isHiddenRuntimeGraphDebugNode(first, {}) &&
                                    !isHiddenRuntimeGraphDebugNode(second, {}) &&
                                    !debugCaptureNodeIds.contains(first) && !debugCaptureNodeIds.contains(second))
                                {
                                    std::string label = readAttribute(line, "label");
                                    if (label.empty())
                                    {
                                        const auto color = readAttribute(line, "color");
                                        if (color == "orangered")
                                            label = "write";
                                        else if (color == "yellowgreen")
                                            label = "read";
                                    }
                                    dotEdges.push_back({std::move(first), std::move(second), std::move(label)});
                                }
                            }
                            else
                            {
                                if (!first.empty() &&
                                    !isHiddenRuntimeGraphDebugNode(first, readAttribute(line, "label")) &&
                                    !debugCaptureNodeIds.contains(first) && !dotNodeIds.contains(first))
                                {
                                    addDotNode(std::move(first),
                                               readAttribute(line, "label"),
                                               readAttribute(line, "fillcolor"));
                                }
                            }
                        }
                        else if (!line.empty())
                        {
                            size_t     pos   = 0;
                            const auto first = readDotId(line, pos);
                            if (isDotRuntimeNodeId(first))
                            {
                                const auto arrow = line.find("->", pos);
                                if (arrow != std::string_view::npos)
                                {
                                    pos = arrow + 2;
                                    std::vector<std::string> targets;
                                    while (pos < line.size())
                                    {
                                        auto target = readDotId(line, pos);
                                        if (target.empty())
                                        {
                                            ++pos;
                                            continue;
                                        }
                                        if (isDotRuntimeNodeId(target))
                                            targets.push_back(std::move(target));
                                    }

                                    std::string label;
                                    const auto  color = readAttribute(line, "color");
                                    if (color == "orangered")
                                        label = "write";
                                    else if (color == "yellowgreen")
                                        label = "read";
                                    for (auto& target : targets)
                                    {
                                        if (first != target)
                                            dotEdges.push_back({first, std::move(target), label});
                                    }
                                }
                                else if (line.find("[label=") != std::string_view::npos)
                                {
                                    addDotNode(first, readHtmlLabelTitle(line), readAttribute(line, "fillcolor"));
                                }
                            }
                        }

                        if (dotLineEnd == std::string::npos)
                            break;
                        dotLineStart = dotLineEnd + 1;
                    }

                    if (!dotNodeOrder.empty() || !dotEdges.empty())
                    {
                        std::vector<Edge> dotRuntimeEdges;
                        dotRuntimeEdges.reserve(dotEdges.size());
                        for (auto& edge : dotEdges)
                        {
                            if (!nodeById.contains(edge.from))
                                addDotNode(edge.from, {}, {});
                            if (!nodeById.contains(edge.to))
                                addDotNode(edge.to, {}, {});
                            dotRuntimeEdges.push_back(
                                {std::move(edge.from), std::move(edge.to), std::move(edge.label)});
                        }
                        edges = std::move(dotRuntimeEdges);
                    }
                }

                if (!dotVisibleNodeIds.empty())
                {
                    std::erase_if(nodeById, [&](const auto& item) { return !dotVisibleNodeIds.contains(item.first); });
                }
            }

            nodes.reserve(nodeById.size());
            for (auto& [id, node] : nodeById)
                nodes.push_back(std::move(node));
            std::unordered_set<std::string> nodeIds;
            nodeIds.reserve(nodes.size());
            for (const auto& node : nodes)
                nodeIds.insert(node.id);
            std::erase_if(edges,
                          [&](const auto& edge) { return !nodeIds.contains(edge.from) || !nodeIds.contains(edge.to); });
            std::unordered_map<std::string, size_t> dotNodeRank;
            dotNodeRank.reserve(dotNodeOrder.size());
            for (size_t i = 0; i < dotNodeOrder.size(); ++i)
                dotNodeRank.emplace(dotNodeOrder[i], i);
            std::sort(nodes.begin(), nodes.end(), [&](const Node& a, const Node& b) {
                const auto aDotRank = dotNodeRank.find(a.id);
                const auto bDotRank = dotNodeRank.find(b.id);
                if (aDotRank != dotNodeRank.end() || bDotRank != dotNodeRank.end())
                {
                    if (aDotRank == dotNodeRank.end())
                        return false;
                    if (bDotRank == dotNodeRank.end())
                        return true;
                    return aDotRank->second < bDotRank->second;
                }

                const auto rank = [](const Node& node) {
                    if (node.kind == "pass")
                        return 0;
                    if (node.imported)
                        return 1;
                    if (node.kind == "resource")
                        return 2;
                    return 3;
                };
                if (rank(a) != rank(b))
                    return rank(a) < rank(b);
                if (a.kind != b.kind)
                    return a.kind < b.kind;
                return a.label < b.label;
            });
        }
    };

    struct RenderGraphWindow::GraphEditorState
    {
        struct Pin
        {
            std::string node;
            std::string slot;
            bool        input {false};
        };

        vrendergraph::RenderGraphRegistry registry;
        vrendergraph::RenderGraphDesc     graph;
        ImNodesEditorContext*             editorContext {nullptr};
        ImNodesEditorContext*             pipelineEditorContext {nullptr};
        std::filesystem::path             path;
        std::filesystem::path             pipelinePath;
        std::string                       pipelineText;
        std::vector<std::string>          pipelineFeatures;
        std::unordered_map<int, Pin>      pins;
        std::string                       status;
        int                               contextNode {0};
        int                               contextLink {0};
        std::string                       pendingPlacementNode;
        ImVec2                            pendingPlacementScreenPos {0.0f, 0.0f};
        ImVec2                            addMenuScreenPos {0.0f, 0.0f};
        bool                              loaded {false};
        bool                              dirty {false};
        bool                              pipelineDirty {false};
        bool                              runtimeDirty {false};
        bool                              applyPositions {false};
        bool                              liveApply {true};
        bool                              editingFeatureInternals {false};
        bool                              focusPipelineEditor {false};
        bool                              directGraphAsset {false};
        bool                              builtinGraphAsset {false};
        bool                              hasPendingPlacement {false};
        bool                              hasAddMenuScreenPos {false};
        std::string                       editingFeature;
        std::string                       loadedUri;

        GraphEditorState()
        {
            editorContext         = ImNodes::EditorContextCreate();
            pipelineEditorContext = ImNodes::EditorContextCreate();

            registerBuiltinRenderGraphResources(registry);

            registerEditorBuiltinRenderGraphPasses(registry);
        }

        ~GraphEditorState()
        {
            if (editorContext)
                ImNodes::EditorContextFree(editorContext);
            if (pipelineEditorContext)
                ImNodes::EditorContextFree(pipelineEditorContext);
        }

        ImNodesEditorContext* activeEditorContext() const
        {
            return editingFeatureInternals ? editorContext : pipelineEditorContext;
        }

        int nodeId(std::string_view kind, std::string_view id) const
        {
            return stableId(std::string(kind) + ":" + std::string(id));
        }

        int pinId(std::string_view node, std::string_view slot, bool input)
        {
            const int id = stableId(std::string(input ? "in:" : "out:") + std::string(node) + ":" + std::string(slot));
            pins[id]     = Pin {std::string(node), std::string(slot), input};
            return id;
        }

        int linkId(std::string_view ref, std::string_view pass, std::string_view slot) const
        {
            return stableId(std::string("link:") + std::string(ref) + "->" + std::string(pass) + "." +
                            std::string(slot));
        }

        int flowLinkId(std::string_view from, std::string_view to) const
        {
            return stableId(std::string("flow:") + std::string(from) + "->" + std::string(to));
        }

        int pipelineLinkId(std::string_view from, std::string_view to) const
        {
            return stableId(std::string("pipeline:") + std::string(from) + "->" + std::string(to));
        }

        std::string metaKeyForResource(std::string_view name) const { return "resource:" + std::string(name); }

        bool isCurrentGraphFeature(std::string_view feature) const
        {
            if (path.empty())
                return false;
            const auto generic = path.generic_string();
            const auto pos     = generic.find("/resources/");
            const auto resPath = pos == std::string::npos ? generic : generic.substr(pos + 11);
            return feature == "res://" + resPath || feature == resPath ||
                   feature.ends_with(path.filename().generic_string());
        }

        std::optional<ImVec2> readNodePos(std::string_view key) const
        {
            if (!graph.meta.is_object() || !graph.meta.contains("editor"))
                return std::nullopt;
            const auto& editor = graph.meta["editor"];
            if (!editor.is_object() || !editor.contains("nodes") || !editor["nodes"].contains(std::string(key)))
                return std::nullopt;
            const auto& node = editor["nodes"][std::string(key)];
            if (!node.is_object() || !node.contains("pos") || !node["pos"].is_array() || node["pos"].size() != 2)
                return std::nullopt;
            return ImVec2 {node["pos"][0].get<float>(), node["pos"][1].get<float>()};
        }

        void storeNodePos(std::string_view key, int id)
        {
            if (!graph.meta.is_object())
                graph.meta = nlohmann::json::object();
            auto& nodes = graph.meta["editor"]["nodes"];
            if (!nodes.is_object())
                nodes = nlohmann::json::object();
            const ImVec2 pos               = ImNodes::GetNodeGridSpacePos(id);
            nodes[std::string(key)]["pos"] = nlohmann::json::array({pos.x, pos.y});
        }

        void storeMeta()
        {
            if (editingFeatureInternals)
            {
                ImNodes::EditorContextSet(editorContext);
                for (const auto& resource : graph.resources)
                    storeNodePos(metaKeyForResource(resource.name), nodeId("resource_in", resource.name));
                for (const auto& pass : graph.passes)
                    storeNodePos(pass.id, nodeId("pass", pass.id));
            }
            else
            {
                ImNodes::EditorContextSet(pipelineEditorContext);
                for (const auto& feature : pipelineFeatures)
                    storeNodePos("feature:" + feature, nodeId("feature", feature));
            }
        }

        void markDirty()
        {
            dirty        = true;
            runtimeDirty = true;
        }

        void markPipelineDirty()
        {
            dirty         = true;
            pipelineDirty = true;
            runtimeDirty  = true;
        }

        bool isFeatureNode(int node, std::string* outFeature = nullptr) const
        {
            for (const auto& feature : pipelineFeatures)
            {
                if (nodeId("feature", feature) != node)
                    continue;
                if (outFeature)
                    *outFeature = feature;
                return true;
            }
            return false;
        }

        bool moveFeatureAfter(std::string_view moved, std::string_view after)
        {
            if (moved == after)
                return false;

            auto movedIt = std::find(pipelineFeatures.begin(), pipelineFeatures.end(), moved);
            auto afterIt = std::find(pipelineFeatures.begin(), pipelineFeatures.end(), after);
            if (movedIt == pipelineFeatures.end() || afterIt == pipelineFeatures.end())
                return false;

            std::string value      = std::move(*movedIt);
            const auto  movedIndex = std::distance(pipelineFeatures.begin(), movedIt);
            const auto  afterIndex = std::distance(pipelineFeatures.begin(), afterIt);
            pipelineFeatures.erase(movedIt);
            auto insertIt = pipelineFeatures.begin() + afterIndex + (movedIndex < afterIndex ? 0 : 1);
            pipelineFeatures.insert(insertIt, std::move(value));
            markPipelineDirty();
            applyPositions = true;
            status         = "Updated feature order";
            return true;
        }

        bool removeEditableLink(int link)
        {
            for (auto& pass : graph.passes)
            {
                for (auto& [slot, ref] : pass.inputs)
                {
                    if (linkId(ref, pass.id, slot) == link)
                    {
                        ref.clear();
                        markDirty();
                        status = "Removed pass input link";
                        return true;
                    }
                }
            }
            status = "This is a read-only dependency edge.";
            return false;
        }

        bool removeEditableNode(int node)
        {
            for (auto it = graph.passes.begin(); it != graph.passes.end(); ++it)
            {
                if (nodeId("pass", it->id) != node)
                    continue;

                const std::string removedId = it->id;
                graph.passes.erase(it);
                for (auto& pass : graph.passes)
                {
                    for (auto& [slot, ref] : pass.inputs)
                    {
                        (void)slot;
                        auto parsed = parseResRef(ref);
                        if (parsed && parsed->node == removedId)
                            ref.clear();
                    }
                }
                markDirty();
                status = "Deleted pass";
                return true;
            }

            const auto before = graph.resources.size();
            graph.resources.erase(std::remove_if(graph.resources.begin(),
                                                 graph.resources.end(),
                                                 [&](const auto& r) {
                                                     return nodeId("resource_in", r.name) == node ||
                                                            nodeId("resource_out", r.name) == node;
                                                 }),
                                  graph.resources.end());
            if (graph.resources.size() == before)
            {
                status = "This node is read-only.";
                return false;
            }

            for (auto& pass : graph.passes)
            {
                for (auto& [slot, ref] : pass.inputs)
                {
                    (void)slot;
                    auto parsed = parseResRef(ref);
                    if (parsed && !hasResource(graph, parsed->node) && !findPass(graph, parsed->node))
                        ref.clear();
                }
            }
            markDirty();
            status = "Deleted resource";
            return true;
        }

        bool removeSelected()
        {
            ImNodes::EditorContextSet(activeEditorContext());
            bool      removed       = false;
            const int selectedLinks = ImNodes::NumSelectedLinks();
            if (selectedLinks > 0)
            {
                std::vector<int> links(static_cast<size_t>(selectedLinks));
                ImNodes::GetSelectedLinks(links.data());
                for (const int link : links)
                    removed = removeEditableLink(link) || removed;
                ImNodes::ClearLinkSelection();
            }

            const int selectedNodes = ImNodes::NumSelectedNodes();
            if (selectedNodes > 0)
            {
                std::vector<int> nodes(static_cast<size_t>(selectedNodes));
                ImNodes::GetSelectedNodes(nodes.data());
                for (const int node : nodes)
                    removed = removeEditableNode(node) || removed;
                ImNodes::ClearNodeSelection();
            }
            if (!removed && selectedLinks == 0 && selectedNodes == 0)
                status = "Nothing selected";
            return removed;
        }
    };

    RenderGraphWindow::RenderGraphWindow() : EditorWindow("Render Graph", ICON_MDI_GRAPH) {}

    RenderGraphWindow::~RenderGraphWindow() = default;

    void RenderGraphWindow::onDestroy(EditorContext& ctx)
    {
        suspendRuntimeGraphWindows(ctx);
        releaseOverlayRenderTarget(ctx);
    }

    void RenderGraphWindow::onClosed(EditorContext& ctx)
    {
        releaseOverlayRenderTarget(ctx);
    }

    void RenderGraphWindow::requestRuntimeFrameGraphViewer()
    {
        m_RuntimeGraphPopupOpen        = true;
        m_RuntimeGraphPopupPendingOpen = true;
        m_RuntimeGraphSuspended        = false;
        m_RuntimeGraphTextureCaptureReadyFrame = static_cast<uint64_t>(ImGui::GetFrameCount()) + 2u;
        if (m_RuntimeGraph)
        {
            m_RuntimeGraph->selectedGraphKey.clear();
            m_RuntimeGraph->selectedGraphIndex = -1;
            m_RuntimeGraph->snapshotHash       = 0;
        }
    }

    void RenderGraphWindow::suspendRuntimeGraphWindows(EditorContext& ctx)
    {
        if (!m_RuntimeGraphCleanupPending && m_RuntimeGraphSuspended && !m_RuntimeGraphPopupOpen &&
            !m_RuntimeTexturePreviewOpen &&
            !m_RuntimeGraph && m_RuntimeTexturePreviewKey.empty() && m_RuntimeTexturePreviewOverrideKey.empty() &&
            m_RuntimeTexturePreviewOverrideKeys.empty() && m_TextureThumbnailCache.empty() &&
            m_RetiredTextureThumbnails.empty() && m_RuntimeGraphTextureAutoFitDone.empty() &&
            m_RuntimeGraphTextureDefaultPreviewDone.empty() &&
            m_RuntimeGraphTexturePreviewSettings.empty() && m_RuntimeGraphTextureAutoFitNextFrame.empty() &&
            m_RuntimeGraphTextureAutoFitDeadlineFrame.empty())
        {
            return;
        }

        if (auto* renderService = ctx.services ? ctx.services->tryGet<vultra::IRenderService>() : nullptr)
        {
            renderService->setFrameGraphTextureCaptureEnabled(false);
            renderService->clearFrameGraphTexturePreviewOverrides();
        }
        m_RuntimeGraph.reset();
        m_RuntimeGraphCleanupPending       = false;
        m_RuntimeGraphPopupOpen              = false;
        m_RuntimeGraphPopupPendingOpen       = false;
        m_RuntimeTexturePreviewOpen          = false;
        m_RuntimeTexturePreviewPopupPendingOpen = false;
        m_RuntimeTexturePreviewKey.clear();
        m_RuntimeTexturePreviewTitle.clear();
        m_RuntimeTexturePreviewDefaultsKey.clear();
        m_RuntimeTexturePreviewOverrideKey.clear();
        m_RuntimeTexturePreviewOverrideKeys.clear();
        m_RuntimeTexturePreviewScale        = 1.0f;
        m_RuntimeTexturePreviewAutoFit      = true;
        m_RuntimeTexturePreviewGammaCorrect = true;
        m_RuntimeTexturePreviewChannels[0]  = true;
        m_RuntimeTexturePreviewChannels[1]  = true;
        m_RuntimeTexturePreviewChannels[2]  = true;
        m_RuntimeTexturePreviewChannels[3]  = false;
        m_RuntimeTexturePreviewMode         = 0;
        m_RuntimeTexturePreviewDepthNear    = 0.1f;
        m_RuntimeTexturePreviewDepthFar     = 1000.0f;
        m_RuntimeTexturePreviewClampMin     = 0.0f;
        m_RuntimeTexturePreviewClampMax     = 1.0f;
        m_RuntimeGraphPreviewScale          = 1.0f;
        m_RuntimeGraphPreviewAutoFit        = true;
        m_PendingRuntimeTexturePreviewAutoFitKey.clear();
        m_PendingRuntimeTexturePreviewAutoFitTexture       = nullptr;
        m_PendingRuntimeTexturePreviewAutoFitFrame         = 0u;
        m_PendingRuntimeTexturePreviewAutoFitDeadlineFrame = 0u;
        m_PendingRuntimeTexturePreviewAutoFitNextTryFrame  = 0u;
        m_RuntimeGraphTextureCaptureReadyFrame             = 0u;
        m_RuntimeGraphTextureAutoFitDone.clear();
        m_RuntimeGraphTextureDefaultPreviewDone.clear();
        m_RuntimeGraphTexturePreviewSettings.clear();
        m_RuntimeGraphTextureAutoFitNextFrame.clear();
        m_RuntimeGraphTextureAutoFitDeadlineFrame.clear();
        ImGuiGraphNode::ClearNodeGraphCaches();
        releaseTextureThumbnails(ctx);
        m_RuntimeGraphSuspended = true;
    }

    void RenderGraphWindow::draw(EditorContext& ctx)
    {
        const EditorCpuScope perf {ctx, "Editor::RenderGraph/Window"};
        resetOverlayRenderTargetForProject(ctx);

        ImGuiWindowFlags windowFlags = ImGuiWindowFlags_None;
        if (m_GraphEditor && m_GraphEditor->dirty)
            windowFlags |= ImGuiWindowFlags_UnsavedDocument;

        const bool visible = ImGui::Begin(title().c_str(), &m_Open, windowFlags);
        if (!visible)
        {
            if (!m_RuntimeGraphPopupOpen && !m_RuntimeTexturePreviewOpen)
                suspendRuntimeGraphWindows(ctx);
            if (!m_RuntimeGraphPopupOpen)
                releaseOverlayRenderTarget(ctx);
            ImGui::End();
            return;
        }
        m_RuntimeGraphSuspended = false;

        drawGraphEditor(ctx);
        ImGui::End();
    }

    void RenderGraphWindow::drawRuntimeFrameGraphViewer(EditorContext& ctx)
    {
        if (m_RuntimeGraphCleanupPending)
        {
            const bool reopenRequested = m_RuntimeGraphPopupOpen || m_RuntimeGraphPopupPendingOpen;
            suspendRuntimeGraphWindows(ctx);
            if (reopenRequested)
                requestRuntimeFrameGraphViewer();
        }
        drawRuntimeGraphPopup(ctx);
    }

    void RenderGraphWindow::drawRuntimeGraph(EditorContext& ctx)
    {
        const EditorCpuScope perf {ctx, "Editor::RenderGraph/RuntimeGraph"};
        if (!m_RuntimeGraph)
            m_RuntimeGraph = std::make_unique<RuntimeGraphState>();

        auto* renderService = ctx.services ? ctx.services->tryGet<vultra::IRenderService>() : nullptr;
        if (renderService)
        {
            renderService->setFrameGraphTextureCaptureEnabled(true);
            bool       defaultChannels[4] {true, true, true, true};
            const auto selectedTextureKey = std::string(vultra::FrameGraphTexturePreviewSettings::kCaptureAllTextures);
            const auto previewSettings = ui::makeFrameGraphTexturePreviewSettings(selectedTextureKey,
                                                                                  true,
                                                                                  defaultChannels,
                                                                                  0,
                                                                                  0.1f,
                                                                                  1000.0f,
                                                                                  0.0f,
                                                                                  1.0f,
                                                                                  kRuntimeGraphThumbnailMaxExtent);
            renderService->setFrameGraphTexturePreviewSettings(previewSettings);
        }
        const std::string snapshot =
            renderService ? std::string(renderService->lastFrameGraphSnapshot()) : std::string {};
        auto& graph = *m_RuntimeGraph;
        {
            const EditorCpuScope parsePerf {ctx, "Editor::RenderGraph/RuntimeParseSnapshot"};
            graph.parse(snapshot);
        }
        collectRetiredTextureThumbnails(ctx);

        size_t debugTextureCount         = 0;
        size_t capturedTextureCount      = 0;
        size_t graphTextureCount         = 0;
        size_t graphCapturedTextureCount = 0;
        if (renderService)
        {
            for (const auto& texture : renderService->frameGraphDebugTextures())
            {
                ++debugTextureCount;
                if (texture.texture)
                    ++capturedTextureCount;
                if (texture.camera == graph.camera)
                {
                    ++graphTextureCount;
                    if (texture.texture)
                        ++graphCapturedTextureCount;
                }
            }
        }

        if (!graph.graphLabels.empty())
        {
            ImGui::SetNextItemWidth(240.0f);
            const char* preview = graph
                                      .graphLabels[static_cast<size_t>(std::clamp(
                                          graph.selectedGraphIndex, 0, static_cast<int>(graph.graphLabels.size()) - 1))]
                                      .c_str();
            if (ImGui::BeginCombo("##RuntimeGraphCamera", preview))
            {
                for (int i = 0; i < static_cast<int>(graph.graphLabels.size()); ++i)
                {
                    const bool selected = i == graph.selectedGraphIndex;
                    if (ImGui::Selectable(graph.graphLabels[static_cast<size_t>(i)].c_str(), selected))
                    {
                        graph.selectedGraphIndex = i;
                        graph.selectedGraphKey   = graph.graphKeys[static_cast<size_t>(i)];
                        graph.snapshotHash       = 0;
                        graph.parse(snapshot);
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }
        if (renderService)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("textures: %zu/%zu captured, graph: %zu/%zu",
                                capturedTextureCount,
                                debugTextureCount,
                                graphCapturedTextureCount,
                                graphTextureCount);
        }
        ImGui::SameLine();
        ImGui::Checkbox("Auto Fit", &m_RuntimeGraphPreviewAutoFit);
        ImGui::SameLine();
        ImGui::BeginDisabled(m_RuntimeGraphPreviewAutoFit);
        ImGui::SetNextItemWidth(120.0f);
        ImGui::SliderFloat("Scale", &m_RuntimeGraphPreviewScale, 0.65f, 1.5f, "%.2fx");
        ImGui::EndDisabled();

        ImGui::Separator();
        if (snapshot.empty())
        {
            ImGui::TextDisabled("No frame graph has been compiled yet.");
            return;
        }

        ImGui::BeginChild(
            "##RuntimeFrameGraphNodes", ImGui::GetContentRegionAvail(), true, ImGuiWindowFlags_HorizontalScrollbar);
        const ImVec2 graphViewport = ImGui::GetContentRegionAvail();

        constexpr float                                                        nodeWidth          = 340.0f;
        constexpr float                                                        graphPixelsPerUnit = 100.0f;
        const bool textureCaptureReady =
            static_cast<uint64_t>(ImGui::GetFrameCount()) >= m_RuntimeGraphTextureCaptureReadyFrame;
        std::unordered_map<std::string, const vultra::FrameGraphDebugTexture*> textureByExactKey;
        std::unordered_map<std::string, const vultra::FrameGraphDebugTexture*> textureByLabel;
        if (renderService && textureCaptureReady)
        {
            const EditorCpuScope lookupPerf {ctx, "Editor::RenderGraph/RuntimeTextureLookup"};
            auto rendererMatches = [&](const vultra::FrameGraphDebugTexture& texture) {
                return graph.renderer.empty() || texture.renderer == graph.renderer;
            };
            auto addLabelKeys = [&](const vultra::FrameGraphDebugTexture& texture, const bool replace) {
                addTextureLookupKey(textureByLabel, texture.name, texture, replace);
                const auto slash = texture.resourceKey.find('/');
                if (slash != std::string::npos && slash + 1 < texture.resourceKey.size())
                    addTextureLookupKey(
                        textureByLabel, std::string_view {texture.resourceKey}.substr(slash + 1), texture, replace);
            };

            for (const auto& texture : renderService->frameGraphDebugTextures())
            {
                if (rendererMatches(texture))
                    addLabelKeys(texture, false);
            }
            if (!graph.textureCamera.empty())
            {
                for (const auto& texture : renderService->frameGraphDebugTextures())
                {
                    if (rendererMatches(texture) && texture.camera == graph.textureCamera)
                        addLabelKeys(texture, true);
                }
            }
            for (const auto& texture : renderService->frameGraphDebugTextures())
            {
                if (!rendererMatches(texture) || texture.camera != graph.camera)
                    continue;

                // Frame graph resource ids are local to each camera graph. Only the currently selected graph camera
                // can use exact resource ids safely; cross-camera preview falls back to labels within the same
                // renderer.
                addTextureLookupKey(textureByExactKey, texture.key, texture, true);
                addTextureLookupKey(textureByExactKey, texture.resourceKey, texture, true);
                addTextureLookupKeyWithoutLayer(textureByExactKey, texture.transientResourceKey, texture, true);
                addLabelKeys(texture, true);
            }
        }
        auto* imguiService   = ctx.services ? ctx.services->tryGet<vultra::IImGuiService>() : nullptr;
        auto* backendService = ctx.services ? ctx.services->tryGet<vultra::IRenderBackendService>() : nullptr;

        auto findDebugTextureForNode =
            [&](const RuntimeGraphState::Node& node) -> const vultra::FrameGraphDebugTexture* {
            if (node.kind != "resource")
                return nullptr;
            if (auto it = textureByExactKey.find(normalizedTextureLookupKey(node.id)); it != textureByExactKey.end())
                return it->second;
            if (node.id.size() > 1 && node.id.front() == 'R' &&
                std::isdigit(static_cast<unsigned char>(node.id[1])) != 0)
            {
                size_t end = 1;
                while (end < node.id.size() && std::isdigit(static_cast<unsigned char>(node.id[end])) != 0)
                    ++end;
                if (end > 1 && end < node.id.size() && node.id[end] == '_')
                {
                    const auto transientKey = "resource:" + node.id.substr(1, end - 1);
                    if (auto it = textureByExactKey.find(normalizedTextureLookupKey(transientKey));
                        it != textureByExactKey.end())
                        return it->second;
                }
            }
            if (auto versionedLabelIt =
                    textureByLabel.find(normalizedTextureLookupKey(runtimeGraphNodeDisplayLabel(
                        node.label, node.kind, node.version)));
                versionedLabelIt != textureByLabel.end())
                return versionedLabelIt->second;
            if (auto labelIt = textureByLabel.find(normalizedTextureLookupKey(node.label));
                labelIt != textureByLabel.end())
                return labelIt->second;
            return nullptr;
        };

        auto makeThumbnailPreviewSettings =
            [&](const vultra::FrameGraphDebugTexture& debugTexture, const float clampMin = 0.0f,
                const float clampMax = 1.0f) {
            bool channels[4] {true, true, true, false};
            return ui::makeFrameGraphTexturePreviewSettings(debugTexture.resourceKey,
                                                            ui::shouldGammaCorrectTexturePreview(debugTexture),
                                                            channels,
                                                            ui::defaultTexturePreviewMode(debugTexture),
                                                            std::max(debugTexture.zNear, 0.0001f),
                                                            std::max(debugTexture.zFar, debugTexture.zNear + 0.0001f),
                                                            clampMin,
                                                            clampMax,
                                                            kRuntimeGraphThumbnailMaxExtent);
        };

        auto fitTexturePreviewClamp = [&](const vultra::FrameGraphDebugTexture& debugTexture, float& outMin,
                                          float& outMax) {
            if (!backendService || !debugTexture.texture)
                return false;
            const auto pixels = backendService->renderDevice().readTextureRGBA8(*debugTexture.texture);
            if (!pixels)
                return false;

            float      minValue     = 1.0f;
            float      maxValue     = 0.0f;
            bool       found        = false;
            const auto sampleCountX = std::min<uint32_t>(64u, std::max(debugTexture.extent.width, 1u));
            const auto sampleCountY = std::min<uint32_t>(64u, std::max(debugTexture.extent.height, 1u));
            for (uint32_t sy = 0; sy < sampleCountY; ++sy)
            {
                const auto y =
                    std::min(debugTexture.extent.height - 1u,
                             static_cast<uint32_t>((static_cast<uint64_t>(sy) * debugTexture.extent.height) /
                                                   sampleCountY));
                for (uint32_t sx = 0; sx < sampleCountX; ++sx)
                {
                    const auto x =
                        std::min(debugTexture.extent.width - 1u,
                                 static_cast<uint32_t>((static_cast<uint64_t>(sx) * debugTexture.extent.width) /
                                                       sampleCountX));
                    const auto offset = (static_cast<uint64_t>(y) * debugTexture.extent.width + x) * 4u;
                    if (offset + 2u >= pixels->size())
                        continue;
                    const float r            = static_cast<float>((*pixels)[offset + 0u]) / 255.0f;
                    const float g            = static_cast<float>((*pixels)[offset + 1u]) / 255.0f;
                    const float b            = static_cast<float>((*pixels)[offset + 2u]) / 255.0f;
                    const float displayValue =
                        (ui::defaultTexturePreviewMode(debugTexture) == 0) ? ((r + g + b) / 3.0f) : r;
                    if (displayValue <= 0.001f || displayValue >= 0.999f)
                        continue;
                    minValue = std::min(minValue, displayValue);
                    maxValue = std::max(maxValue, displayValue);
                    found    = true;
                }
            }
            if (!found)
                return false;

            const float padding = std::max((maxValue - minValue) * 0.08f, 1.0f / 255.0f);
            outMin              = std::max(0.0f, minValue - padding);
            outMax              = std::min(1.0f, maxValue + padding);
            ui::normalizePreviewClamp(outMin, outMax);
            return true;
        };

        auto prepareRuntimeTexturePreview = [&](const vultra::FrameGraphDebugTexture& debugTexture) {
            if (!renderService || !debugTexture.capturable)
                return;

            const auto& key = debugTexture.resourceKey;
            if (!m_RuntimeGraphTextureDefaultPreviewDone.contains(key))
            {
                auto settings = makeThumbnailPreviewSettings(debugTexture);
                m_RuntimeGraphTexturePreviewSettings[key] = settings;
                renderService->setFrameGraphTexturePreviewOverride(key, settings);
                m_RuntimeGraphTextureDefaultPreviewDone.insert(key);

                if (ui::isDepthLikeTexture(debugTexture) && !ui::isShadowLikeTexture(debugTexture))
                {
                    const auto frame = static_cast<uint64_t>(ImGui::GetFrameCount());
                    m_RuntimeGraphTextureAutoFitNextFrame[key]     = frame + 3u;
                    m_RuntimeGraphTextureAutoFitDeadlineFrame[key] = frame + 24u;
                    m_RuntimeGraphTextureAutoFitDone.erase(key);
                }
                return;
            }

            if (!ui::isDepthLikeTexture(debugTexture) || ui::isShadowLikeTexture(debugTexture) ||
                m_RuntimeGraphTextureAutoFitDone.contains(key) || !debugTexture.texture)
            {
                return;
            }

            const auto frame       = static_cast<uint64_t>(ImGui::GetFrameCount());
            const auto nextFrameIt = m_RuntimeGraphTextureAutoFitNextFrame.find(key);
            if (nextFrameIt == m_RuntimeGraphTextureAutoFitNextFrame.end() || frame < nextFrameIt->second)
                return;

            float clampMin = 0.0f;
            float clampMax = 1.0f;
            if (fitTexturePreviewClamp(debugTexture, clampMin, clampMax))
            {
                auto settings = makeThumbnailPreviewSettings(debugTexture, clampMin, clampMax);
                m_RuntimeGraphTexturePreviewSettings[key] = settings;
                renderService->setFrameGraphTexturePreviewOverride(key, settings);
                m_RuntimeGraphTextureAutoFitDone.insert(key);
                m_RuntimeGraphTextureAutoFitNextFrame.erase(key);
                m_RuntimeGraphTextureAutoFitDeadlineFrame.erase(key);
            }
            else if (auto deadlineIt = m_RuntimeGraphTextureAutoFitDeadlineFrame.find(key);
                     deadlineIt != m_RuntimeGraphTextureAutoFitDeadlineFrame.end() && frame < deadlineIt->second)
            {
                m_RuntimeGraphTextureAutoFitNextFrame[key] = frame + 6u;
            }
            else
            {
                m_RuntimeGraphTextureAutoFitDone.insert(key);
                m_RuntimeGraphTextureAutoFitNextFrame.erase(key);
                m_RuntimeGraphTextureAutoFitDeadlineFrame.erase(key);
            }
        };

        std::unordered_map<std::string, const vultra::FrameGraphDebugTexture*> nodeTextures;
        nodeTextures.reserve(graph.nodes.size());
        for (const auto& node : graph.nodes)
        {
            if (auto* debugTexture = findDebugTextureForNode(node); debugTexture)
                nodeTextures.emplace(node.id, debugTexture);
        }

        std::unordered_map<std::string, const RuntimeGraphState::Node*> nodeById;
        nodeById.reserve(graph.nodes.size());
        for (const auto& node : graph.nodes)
            nodeById.emplace(node.id, &node);

        auto concreteRuntimeGraphNodeLabel = [&](const RuntimeGraphState::Node& node) {
            if (!isAnonymousRuntimeGraphDebugId(node.id, node.label))
                return runtimeGraphNodeDisplayLabel(node.label, node.kind, node.version);

            if (auto textureIt = nodeTextures.find(node.id); textureIt != nodeTextures.end() && textureIt->second)
                return runtimeGraphNodeDisplayLabel(textureIt->second->name, node.kind, node.version);

            auto otherLabel = [&](std::string_view otherId) -> std::string {
                const auto it = nodeById.find(std::string(otherId));
                if (it == nodeById.end() || !it->second)
                    return {};
                const auto& other = *it->second;
                return isAnonymousRuntimeGraphDebugId(other.id, other.label) ? std::string {} : other.label;
            };

            if (node.kind == "resource")
            {
                for (const auto& edge : graph.edges)
                {
                    if (edge.to != node.id || edge.label != "write")
                        continue;
                    auto                       writer       = otherLabel(edge.from);
                    constexpr std::string_view uploadPrefix = "Upload";
                    if (writer.starts_with(uploadPrefix) && writer.size() > uploadPrefix.size())
                        return runtimeGraphNodeDisplayLabel(
                            writer.substr(uploadPrefix.size()), node.kind, node.version);
                }
            }
            else if (node.kind == "pass")
            {
                for (const auto& edge : graph.edges)
                {
                    if (edge.from != node.id || edge.label != "write")
                        continue;
                    if (auto textureIt = nodeTextures.find(edge.to);
                        textureIt != nodeTextures.end() && textureIt->second)
                    {
                        auto                       name        = textureIt->second->name;
                        constexpr std::string_view colorSuffix = " Color";
                        if (name.ends_with(colorSuffix))
                            return name.substr(0, name.size() - colorSuffix.size()) + "Pass";
                        return name + " Writer";
                    }
                }
            }

            return runtimeGraphNodeDisplayLabel(node.label, node.kind, node.version);
        };

        auto copyRuntimeGraphDot = [&]() {
            if (!graph.rawDot.empty())
            {
                ImGui::SetClipboardText(graph.rawDot.c_str());
                return;
            }

            std::ostringstream dot;
            dot << "digraph RuntimeFrameGraph {\n";
            dot << "  graph [rankdir=TB, nodesep=0.38, ranksep=0.55, margin=0.08, concentrate=true];\n";
            dot << "  node [shape=box, style=\"rounded,filled\", fontname=\"Inter\", fontsize=10];\n";
            dot << "  edge [fontname=\"Inter\", fontsize=9, arrowsize=0.7];\n\n";
            for (const auto& node : graph.nodes)
            {
                const bool resourceNode = node.kind == "resource";
                const bool hasTexture   = nodeTextures.contains(node.id);
                const auto displayLabel = concreteRuntimeGraphNodeLabel(node);
                const auto fillColor =
                    resourceNode ? (node.imported ? "#547ab0" : "#468a94") : (node.sideEffect ? "#ac763a" : "#568852");
                const auto shape = hasTexture ? "box" : "box";
                dot << "  \"" << dotEscape(node.id) << "\" [label=\"" << dotEscape(displayLabel) << "\", fillcolor=\""
                    << fillColor << "\", color=\"#4e606e\", shape=" << shape << "];\n";
            }
            dot << "\n";
            for (const auto& edge : graph.edges)
            {
                const auto color = edge.label == "read" ? "#5299cf" : edge.label == "write" ? "#75bd73" : "#a8a8a8";
                dot << "  \"" << dotEscape(edge.from) << "\" -> \"" << dotEscape(edge.to) << "\" [color=\"" << color
                    << "\"];\n";
            }
            dot << "}\n";
            ImGui::SetClipboardText(dot.str().c_str());
        };

        if (ImGui::Button("Copy DOT"))
            copyRuntimeGraphDot();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(graph.rawDot.empty() ? "Copy a reconstructed Graphviz DOT fallback" :
                                                     "Copy the selected runtime graph DOT emitted by FrameGraph");

        if (ImGuiGraphNode::BeginNodeGraph("RuntimeFrameGraphDot", ImGuiGraphNodeLayout_Dot, graphPixelsPerUnit))
        {
            const EditorCpuScope nodeGraphPerf {ctx, "Editor::RenderGraph/RuntimeImGuiGraphNode"};
            const bool useRawDotLayout =
                !graph.rawDot.empty() && ImGuiGraphNode::NodeGraphLoadDot(graph.rawDot.c_str());
            ImGuiGraphNode::NodeGraphSetView(
                graphViewport, m_RuntimeGraphPreviewScale, m_RuntimeGraphPreviewAutoFit, true);
            if (!useRawDotLayout)
            {
                ImGuiGraphNode::NodeGraphSetGraphAttribute("rankdir", "TB");
                ImGuiGraphNode::NodeGraphSetGraphAttribute("nodesep", "0.38");
                ImGuiGraphNode::NodeGraphSetGraphAttribute("ranksep", "0.55");
                ImGuiGraphNode::NodeGraphSetGraphAttribute("margin", "0.08");
                ImGuiGraphNode::NodeGraphSetGraphAttribute("concentrate", "true");
            }
            for (const auto& node : graph.nodes)
            {
                const bool   resourceNode = node.kind == "resource";
                const bool   hasTexture   = nodeTextures.contains(node.id);
                const float  height       = hasTexture ? 276.0f : 36.0f;
                const ImVec4 titleColor =
                    resourceNode ?
                        (node.imported ? ImVec4 {0.33f, 0.48f, 0.69f, 1.0f} : ImVec4 {0.27f, 0.54f, 0.58f, 1.0f}) :
                        (node.sideEffect ? ImVec4 {0.67f, 0.46f, 0.23f, 1.0f} : ImVec4 {0.34f, 0.53f, 0.32f, 1.0f});
                const ImVec4 bodyColor =
                    resourceNode ?
                        (node.imported ? ImVec4 {0.10f, 0.13f, 0.19f, 1.0f} : ImVec4 {0.09f, 0.16f, 0.17f, 1.0f}) :
                        (node.sideEffect ? ImVec4 {0.19f, 0.15f, 0.09f, 1.0f} : ImVec4 {0.11f, 0.16f, 0.11f, 1.0f});
                if (!useRawDotLayout)
                {
                    ImGuiGraphNode::NodeGraphAddNodeSized(
                        node.id.c_str(),
                        ImVec2 {nodeWidth / graphPixelsPerUnit, height / graphPixelsPerUnit},
                        titleColor,
                        bodyColor);
                }

                const auto  displayLabel = concreteRuntimeGraphNodeLabel(node);
                const auto  title        = runtimeGraphDisplayLabel(displayLabel);
                const char* inputLabel   = resourceNode    ? (node.imported ? "import" : "read") :
                                           node.sideEffect ? "in\nside effect" :
                                                             "in";
                const char* outputLabel  = resourceNode ? "use" : (node.sideEffect ? "side" : "out");
                const auto  titleColorU32 =
                    resourceNode ? (node.imported ? IM_COL32(84, 122, 176, 255) : IM_COL32(70, 138, 148, 255)) :
                                    (node.sideEffect ? IM_COL32(172, 118, 58, 255) : IM_COL32(86, 136, 82, 255));
                const auto bodyColorU32 = resourceNode ?
                                              (node.imported ? IM_COL32(26, 34, 48, 255) : IM_COL32(24, 42, 44, 255)) :
                                              (node.sideEffect ? IM_COL32(48, 37, 24, 255) : IM_COL32(29, 42, 29, 255));

                ImGuiGraphNodeRuntimeNodeStyle style {
                    .id             = node.id.c_str(),
                    .title          = title.c_str(),
                    .tooltip        = displayLabel.c_str(),
                    .inputLabel     = inputLabel,
                    .outputLabel    = outputLabel,
                    .metadata       = nullptr,
                    .titleColor     = titleColorU32,
                    .bodyColor      = bodyColorU32,
                    .borderColor    = IM_COL32(78, 96, 110, 220),
                    .textColor      = IM_COL32(235, 242, 248, 255),
                    .mutedTextColor = IM_COL32(150, 166, 182, 255),
                    .pinColor       = IM_COL32(74, 148, 220, 255),
                };

                std::string metadata;
                if (resourceNode)
                {
                    auto textureIt = nodeTextures.find(node.id);
                    if (textureIt != nodeTextures.end())
                    {
                        const auto* debugTexture = textureIt->second;
                        prepareRuntimeTexturePreview(*debugTexture);
                        if (debugTexture->texture && imguiService)
                        {
                            auto& cached = m_TextureThumbnailCache[debugTexture->key];
                            if (cached.texture != debugTexture->texture)
                            {
                                if (cached.textureId)
                                {
                                    cached.retireFrame =
                                        static_cast<uint64_t>(ImGui::GetFrameCount()) + kRenderTargetReleaseDelayFrames;
                                    m_RetiredTextureThumbnails.push_back(cached);
                                }
                                cached.texture = debugTexture->texture;
                                cached.textureId =
                                    imguiService->addTexture(*debugTexture->texture, makeLinearClampSampler(ctx));
                                cached.retireFrame = 0;
                            }
                            style.textureId  = cached.textureId;
                            style.hasTexture = true;
                        }
                        metadata = debugTexture->camera + " | " + std::to_string(debugTexture->sourceExtent.width) +
                                   "x" + std::to_string(debugTexture->sourceExtent.height);
                        style.metadata = metadata.c_str();
                    }
                }
                ImGuiGraphNode::NodeGraphSetRuntimeNodeStyle(style);
            }
            if (!useRawDotLayout)
            {
                for (const auto& edge : graph.edges)
                {
                    const ImVec4 color = edge.label == "read"  ? ImVec4 {0.32f, 0.60f, 0.81f, 0.88f} :
                                         edge.label == "write" ? ImVec4 {0.46f, 0.74f, 0.45f, 0.92f} :
                                                                 ImVec4 {0.66f, 0.66f, 0.66f, 0.84f};
                    ImGuiGraphNode::NodeGraphAddEdge((edge.from + "->" + edge.to + ":" + edge.label).c_str(),
                                                     edge.from.c_str(),
                                                     edge.to.c_str(),
                                                     color);
                }
            }
            ImGuiGraphNode::EndNodeGraph();
        }

        for (const auto& node : graph.nodes)
        {
            if (ImGuiGraphNode::WasRuntimeNodeTextureDoubleClicked(node.id.c_str()))
            {
                if (auto textureIt = nodeTextures.find(node.id); textureIt != nodeTextures.end() && textureIt->second)
                {
                    m_RuntimeTexturePreviewKey         = textureIt->second->resourceKey;
                    m_RuntimeTexturePreviewTitle       = textureIt->second->name;
                    m_RuntimeTexturePreviewOpen        = true;
                    m_RuntimeTexturePreviewPopupPendingOpen = true;
                    m_RuntimeTexturePreviewDefaultsKey.clear();
                    if (auto settingsIt = m_RuntimeGraphTexturePreviewSettings.find(textureIt->second->resourceKey);
                        settingsIt != m_RuntimeGraphTexturePreviewSettings.end())
                    {
                        const auto& settings                = settingsIt->second;
                        m_RuntimeTexturePreviewGammaCorrect = settings.gammaCorrect;
                        m_RuntimeTexturePreviewMode         = settings.previewMode;
                        m_RuntimeTexturePreviewDepthNear    = settings.depthNear;
                        m_RuntimeTexturePreviewDepthFar     = settings.depthFar;
                        m_RuntimeTexturePreviewClampMin     = settings.clampMin;
                        m_RuntimeTexturePreviewClampMax     = settings.clampMax;
                        for (int i = 0; i < 4; ++i)
                            m_RuntimeTexturePreviewChannels[i] = settings.channels[i];
                        m_RuntimeTexturePreviewDefaultsKey = textureIt->second->resourceKey;
                    }
                }
            }
        }
        ImGui::EndChild();
    }

    void RenderGraphWindow::drawRuntimeGraphPopup(EditorContext& ctx)
    {
        const EditorCpuScope perf {ctx, "Editor::RenderGraph/RuntimePopup"};
        if (!m_RuntimeGraphPopupOpen && !m_RuntimeGraphPopupPendingOpen)
            return;

        if (m_RuntimeGraphPopupPendingOpen)
        {
            ImGui::OpenPopup("Runtime Frame Graph Viewer");
            m_RuntimeGraphPopupPendingOpen = false;
        }

        ImGui::SetNextWindowSize(ImVec2 {1280.0f, 820.0f}, ImGuiCond_Appearing);
        bool popupOpen = m_RuntimeGraphPopupOpen;
        if (ImGui::BeginPopupModal("Runtime Frame Graph Viewer",
                                   &popupOpen,
                                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings))
        {
            m_RuntimeGraphPopupOpen = popupOpen;
            if (m_RuntimeGraphPopupOpen)
            {
                drawRuntimeGraph(ctx);
                drawRuntimeTexturePreviewWindow(ctx);
            }
            ImGui::EndPopup();
        }
        else
        {
            m_RuntimeGraphPopupOpen = popupOpen;
        }
        if (!m_RuntimeGraphPopupOpen)
        {
            m_RuntimeTexturePreviewOpen             = false;
            m_RuntimeTexturePreviewPopupPendingOpen = false;
            m_RuntimeGraphCleanupPending            = true;
        }
    }

    void RenderGraphWindow::drawRuntimeTexturePreviewWindow(EditorContext& ctx)
    {
        const EditorCpuScope perf {ctx, "Editor::RenderGraph/RuntimeTexturePreview"};
        auto* renderService  = ctx.services ? ctx.services->tryGet<vultra::IRenderService>() : nullptr;
        auto* imguiService   = ctx.services ? ctx.services->tryGet<vultra::IImGuiService>() : nullptr;
        auto* backendService = ctx.services ? ctx.services->tryGet<vultra::IRenderBackendService>() : nullptr;
        auto  restoreRuntimeTexturePreviewOverride = [&](const std::string& key) {
            if (!renderService || key.empty())
                return;
            if (auto settingsIt = m_RuntimeGraphTexturePreviewSettings.find(key);
                settingsIt != m_RuntimeGraphTexturePreviewSettings.end())
                renderService->setFrameGraphTexturePreviewOverride(key, settingsIt->second);
            else
                renderService->clearFrameGraphTexturePreviewOverride(key);
        };
        auto restoreRuntimeTexturePreviewOverrides = [&]() {
            for (const auto& key : m_RuntimeTexturePreviewOverrideKeys)
                restoreRuntimeTexturePreviewOverride(key);
            m_RuntimeTexturePreviewOverrideKeys.clear();
            restoreRuntimeTexturePreviewOverride(m_RuntimeTexturePreviewOverrideKey);
            m_RuntimeTexturePreviewOverrideKey.clear();
        };
        const bool textureCaptureReady =
            static_cast<uint64_t>(ImGui::GetFrameCount()) >= m_RuntimeGraphTextureCaptureReadyFrame;
        if (!textureCaptureReady || !m_RuntimeTexturePreviewOpen || m_RuntimeTexturePreviewKey.empty())
        {
            restoreRuntimeTexturePreviewOverrides();
            m_PendingRuntimeTexturePreviewAutoFitKey.clear();
            m_PendingRuntimeTexturePreviewAutoFitTexture       = nullptr;
            m_PendingRuntimeTexturePreviewAutoFitFrame         = 0u;
            m_PendingRuntimeTexturePreviewAutoFitNextTryFrame  = 0u;
            m_PendingRuntimeTexturePreviewAutoFitDeadlineFrame = 0u;
            m_RuntimeTexturePreviewPopupPendingOpen = false;
            return;
        }
        if (!renderService || !imguiService)
            return;

        bool       defaultChannels[4] {true, true, true, true};
        const auto captureSettings = ui::makeFrameGraphTexturePreviewSettings(m_RuntimeTexturePreviewKey,
                                                                              true,
                                                                              defaultChannels,
                                                                              0,
                                                                              0.1f,
                                                                              1000.0f,
                                                                              0.0f,
                                                                              1.0f,
                                                                              0u);
        renderService->setFrameGraphTexturePreviewOverride(m_RuntimeTexturePreviewKey, captureSettings);
        m_RuntimeTexturePreviewOverrideKey = m_RuntimeTexturePreviewKey;

        const vultra::FrameGraphDebugTexture* debugTexture = nullptr;
        for (const auto& texture : renderService->frameGraphDebugTextures())
        {
            if (texture.resourceKey == m_RuntimeTexturePreviewKey)
            {
                debugTexture = &texture;
                break;
            }
        }
        if (!debugTexture)
        {
            restoreRuntimeTexturePreviewOverrides();
            m_PendingRuntimeTexturePreviewAutoFitKey.clear();
            m_PendingRuntimeTexturePreviewAutoFitTexture       = nullptr;
            m_PendingRuntimeTexturePreviewAutoFitFrame         = 0u;
            m_PendingRuntimeTexturePreviewAutoFitNextTryFrame  = 0u;
            m_PendingRuntimeTexturePreviewAutoFitDeadlineFrame = 0u;
            m_RuntimeTexturePreviewPopupPendingOpen = false;
            return;
        }
        const bool xrRelatedGraph = m_RuntimeGraph && m_RuntimeGraph->isXrRelated();
        const auto stereoPair =
            xrRelatedGraph ? findRuntimeStereoTexturePair(renderService->frameGraphDebugTextures(), *debugTexture) :
                             RuntimeStereoTexturePair {};
        const bool showStereoPreview = stereoPair.left && stereoPair.right && stereoPair.left->texture &&
                                       stereoPair.right->texture;
        if (!debugTexture->texture)
        {
            std::string title = m_RuntimeTexturePreviewTitle.empty() ? debugTexture->name : m_RuntimeTexturePreviewTitle;
            title += "##RuntimeTexturePreview";
            ImGui::SetNextWindowSize(ImVec2 {520.0f, 180.0f}, ImGuiCond_Appearing);
            if (m_RuntimeTexturePreviewPopupPendingOpen)
            {
                ImGui::OpenPopup(title.c_str());
                m_RuntimeTexturePreviewPopupPendingOpen = false;
            }
            if (ImGui::BeginPopupModal(title.c_str(), &m_RuntimeTexturePreviewOpen, ImGuiWindowFlags_NoCollapse))
            {
                ImGui::TextDisabled("%s | %ux%u",
                                    debugTexture->name.c_str(),
                                    debugTexture->sourceExtent.width,
                                    debugTexture->sourceExtent.height);
                ImGui::Separator();
                ImGui::TextUnformatted("Capturing selected texture preview...");
                ImGui::EndPopup();
            }
            return;
        }

        auto ensureTexturePreviewId = [&](const vultra::FrameGraphDebugTexture& texture) {
            auto& cached = m_TextureThumbnailCache[texture.key];
            if (cached.texture != texture.texture)
            {
                if (cached.textureId)
                {
                    cached.retireFrame =
                        static_cast<uint64_t>(ImGui::GetFrameCount()) + kRenderTargetReleaseDelayFrames;
                    m_RetiredTextureThumbnails.push_back(cached);
                }
                cached.texture     = texture.texture;
                cached.textureId   = texture.texture ? imguiService->addTexture(*texture.texture, makeLinearClampSampler(ctx)) :
                                                        vultra::IImGuiService::TextureID {};
                cached.retireFrame = 0;
            }
            return cached.textureId;
        };
        const auto primaryTextureId = ensureTexturePreviewId(*debugTexture);
        vultra::IImGuiService::TextureID leftTextureId {};
        vultra::IImGuiService::TextureID rightTextureId {};
        if (showStereoPreview)
        {
            leftTextureId  = ensureTexturePreviewId(*stereoPair.left);
            rightTextureId = ensureTexturePreviewId(*stereoPair.right);
        }

        std::string title = m_RuntimeTexturePreviewTitle.empty() ? debugTexture->name : m_RuntimeTexturePreviewTitle;
        title += "##RuntimeTexturePreview";
        ImGui::SetNextWindowSize(ImVec2 {960.0f, 720.0f}, ImGuiCond_Appearing);
        if (m_RuntimeTexturePreviewPopupPendingOpen)
        {
            ImGui::OpenPopup(title.c_str());
            m_RuntimeTexturePreviewPopupPendingOpen = false;
        }
        if (ImGui::BeginPopupModal(title.c_str(), &m_RuntimeTexturePreviewOpen, ImGuiWindowFlags_NoCollapse))
        {
            if (m_RuntimeTexturePreviewDefaultsKey != debugTexture->resourceKey)
            {
                m_RuntimeTexturePreviewDepthNear = std::max(debugTexture->zNear, 0.0001f);
                m_RuntimeTexturePreviewDepthFar =
                    std::max(debugTexture->zFar, m_RuntimeTexturePreviewDepthNear + 0.0001f);
                m_RuntimeTexturePreviewClampMin     = 0.0f;
                m_RuntimeTexturePreviewClampMax     = 1.0f;
                m_RuntimeTexturePreviewAutoFit      = true;
                m_RuntimeTexturePreviewGammaCorrect = ui::shouldGammaCorrectTexturePreview(*debugTexture);
                m_RuntimeTexturePreviewMode         = ui::defaultTexturePreviewMode(*debugTexture);
                m_RuntimeTexturePreviewChannels[0]  = true;
                m_RuntimeTexturePreviewChannels[1]  = true;
                m_RuntimeTexturePreviewChannels[2]  = true;
                m_RuntimeTexturePreviewChannels[3]  = false;
                m_RuntimeTexturePreviewDefaultsKey  = debugTexture->resourceKey;
                if (ui::isDepthLikeTexture(*debugTexture) && !ui::isShadowLikeTexture(*debugTexture))
                {
                    m_PendingRuntimeTexturePreviewAutoFitKey           = debugTexture->resourceKey;
                    m_PendingRuntimeTexturePreviewAutoFitTexture       = debugTexture->texture;
                    const auto frame                                   = static_cast<uint64_t>(ImGui::GetFrameCount());
                    m_PendingRuntimeTexturePreviewAutoFitFrame         = frame + 3u;
                    m_PendingRuntimeTexturePreviewAutoFitNextTryFrame  = frame + 3u;
                    m_PendingRuntimeTexturePreviewAutoFitDeadlineFrame = frame + 24u;
                }
                else
                {
                    m_PendingRuntimeTexturePreviewAutoFitKey.clear();
                    m_PendingRuntimeTexturePreviewAutoFitTexture       = nullptr;
                    m_PendingRuntimeTexturePreviewAutoFitFrame         = 0u;
                    m_PendingRuntimeTexturePreviewAutoFitNextTryFrame  = 0u;
                    m_PendingRuntimeTexturePreviewAutoFitDeadlineFrame = 0u;
                }
            }

            const auto formatName = std::string(vultra::rhi::toString(debugTexture->format));
            ImGui::TextDisabled("%s | %ux%u | %s",
                                debugTexture->name.c_str(),
                                debugTexture->sourceExtent.width,
                                debugTexture->sourceExtent.height,
                                formatName.c_str());
            ImGui::SameLine();
            ImGui::Checkbox("Auto Fit", &m_RuntimeTexturePreviewAutoFit);
            ImGui::SameLine();
            ImGui::BeginDisabled(m_RuntimeTexturePreviewAutoFit);
            ImGui::SetNextItemWidth(130.0f);
            ImGui::SliderFloat(
                "Scale", &m_RuntimeTexturePreviewScale, 0.1f, 8.0f, "%.2fx", ImGuiSliderFlags_Logarithmic);
            ImGui::EndDisabled();
            ImGui::SameLine();
            ui::drawSaveFrameGraphTexturePreviewButton(ctx, *debugTexture, "RuntimeGraphSaveTexturePreview");
            ImGui::SameLine();
            static constexpr const char* kPreviewModes[] {
                "Color", "Raw Depth", "Linear Depth", "Inverted Linear Depth", "Alpha", "Normal"};
            constexpr int kPreviewModeCount = static_cast<int>(sizeof(kPreviewModes) / sizeof(kPreviewModes[0]));
            m_RuntimeTexturePreviewMode     = std::clamp(m_RuntimeTexturePreviewMode, 0, kPreviewModeCount - 1);
            ImGui::SetNextItemWidth(160.0f);
            ImGui::PushID(debugTexture->resourceKey.c_str());
            if (ImGui::BeginCombo("##RuntimeTexturePreviewMode", kPreviewModes[m_RuntimeTexturePreviewMode]))
            {
                for (int i = 0; i < kPreviewModeCount; ++i)
                {
                    const bool selected = i == m_RuntimeTexturePreviewMode;
                    if (ImGui::Selectable(kPreviewModes[i], selected))
                        m_RuntimeTexturePreviewMode = i;
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Texture display mode");
            ImGui::SameLine();
            ImGui::TextUnformatted("Mode");
            ImGui::PopID();
            ImGui::Checkbox("Gamma", &m_RuntimeTexturePreviewGammaCorrect);
            ImGui::SameLine();
            ImGui::Checkbox("R", &m_RuntimeTexturePreviewChannels[0]);
            ImGui::SameLine();
            ImGui::Checkbox("G", &m_RuntimeTexturePreviewChannels[1]);
            ImGui::SameLine();
            ImGui::Checkbox("B", &m_RuntimeTexturePreviewChannels[2]);
            ImGui::SameLine();
            ImGui::Checkbox("A", &m_RuntimeTexturePreviewChannels[3]);
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_MDI_RESTORE "##RuntimeTexturePreviewReset"))
            {
                m_RuntimeTexturePreviewDefaultsKey.clear();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Reset preview settings");
            ImGui::SameLine();
            if (ImGui::SmallButton("Fit Range"))
            {
                m_RuntimeTexturePreviewDepthNear = std::max(debugTexture->zNear, 0.0001f);
                m_RuntimeTexturePreviewDepthFar =
                    std::max(debugTexture->zFar, m_RuntimeTexturePreviewDepthNear + 0.0001f);
                m_RuntimeTexturePreviewClampMin = 0.0f;
                m_RuntimeTexturePreviewClampMax = 1.0f;
                if (ui::isDepthLikeTexture(*debugTexture) && !ui::isShadowLikeTexture(*debugTexture))
                {
                    m_PendingRuntimeTexturePreviewAutoFitKey           = debugTexture->resourceKey;
                    m_PendingRuntimeTexturePreviewAutoFitTexture       = debugTexture->texture;
                    const auto frame                                   = static_cast<uint64_t>(ImGui::GetFrameCount());
                    m_PendingRuntimeTexturePreviewAutoFitFrame         = frame + 3u;
                    m_PendingRuntimeTexturePreviewAutoFitNextTryFrame  = frame + 3u;
                    m_PendingRuntimeTexturePreviewAutoFitDeadlineFrame = frame + 24u;
                }
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Auto fit depth range from the preview image");
            ImGui::SetNextItemWidth(120.0f);
            ImGui::DragFloat("Clamp Min", &m_RuntimeTexturePreviewClampMin, 0.001f, 0.0f, 1.0f, "%.4f");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120.0f);
            ImGui::DragFloat("Clamp Max", &m_RuntimeTexturePreviewClampMax, 0.001f, 0.0f, 1.0f, "%.4f");
            ui::normalizePreviewClamp(m_RuntimeTexturePreviewClampMin, m_RuntimeTexturePreviewClampMax);
            if (m_RuntimeTexturePreviewMode == 2 || m_RuntimeTexturePreviewMode == 3)
                ImGui::TextDisabled(
                    "Camera z: %.4f - %.1f", m_RuntimeTexturePreviewDepthNear, m_RuntimeTexturePreviewDepthFar);

            const auto previewSettings = ui::makeFrameGraphTexturePreviewSettings(debugTexture->resourceKey,
                                                                                  m_RuntimeTexturePreviewGammaCorrect,
                                                                                  m_RuntimeTexturePreviewChannels,
                                                                                  m_RuntimeTexturePreviewMode,
                                                                                  m_RuntimeTexturePreviewDepthNear,
                                                                                  m_RuntimeTexturePreviewDepthFar,
                                                                                  m_RuntimeTexturePreviewClampMin,
                                                                                  m_RuntimeTexturePreviewClampMax);
            const auto thumbnailSettings = ui::makeFrameGraphTexturePreviewSettings(debugTexture->resourceKey,
                                                                                    m_RuntimeTexturePreviewGammaCorrect,
                                                                                    m_RuntimeTexturePreviewChannels,
                                                                                    m_RuntimeTexturePreviewMode,
                                                                                    m_RuntimeTexturePreviewDepthNear,
                                                                                    m_RuntimeTexturePreviewDepthFar,
                                                                                    m_RuntimeTexturePreviewClampMin,
                                                                                    m_RuntimeTexturePreviewClampMax,
                                                                                    kRuntimeGraphThumbnailMaxExtent);
            if (!m_RuntimeTexturePreviewOverrideKey.empty() &&
                m_RuntimeTexturePreviewOverrideKey != debugTexture->resourceKey)
            {
                restoreRuntimeTexturePreviewOverride(m_RuntimeTexturePreviewOverrideKey);
            }
            m_RuntimeGraphTexturePreviewSettings[debugTexture->resourceKey] = thumbnailSettings;
            renderService->setFrameGraphTexturePreviewOverride(debugTexture->resourceKey, previewSettings);
            m_RuntimeTexturePreviewOverrideKey = debugTexture->resourceKey;
            m_RuntimeTexturePreviewOverrideKeys.insert(debugTexture->resourceKey);
            if (showStereoPreview)
            {
                const vultra::FrameGraphDebugTexture* stereoTextures[] {stereoPair.left, stereoPair.right};
                for (const auto* stereoTexture : stereoTextures)
                {
                    if (!stereoTexture || stereoTexture->resourceKey == debugTexture->resourceKey)
                        continue;
                    m_RuntimeGraphTexturePreviewSettings[stereoTexture->resourceKey] = thumbnailSettings;
                    renderService->setFrameGraphTexturePreviewOverride(stereoTexture->resourceKey, previewSettings);
                    m_RuntimeTexturePreviewOverrideKeys.insert(stereoTexture->resourceKey);
                }
            }

            auto clearPendingAutoFit = [&]() {
                m_PendingRuntimeTexturePreviewAutoFitKey.clear();
                m_PendingRuntimeTexturePreviewAutoFitTexture       = nullptr;
                m_PendingRuntimeTexturePreviewAutoFitFrame         = 0u;
                m_PendingRuntimeTexturePreviewAutoFitNextTryFrame  = 0u;
                m_PendingRuntimeTexturePreviewAutoFitDeadlineFrame = 0u;
            };
            auto autoFitClamp = [&]() {
                if (!backendService || !debugTexture->texture)
                    return false;
                const auto pixels = backendService->renderDevice().readTextureRGBA8(*debugTexture->texture);
                if (!pixels)
                    return false;

                float      minValue     = 1.0f;
                float      maxValue     = 0.0f;
                bool       found        = false;
                const auto sampleCountX = std::min<uint32_t>(64u, std::max(debugTexture->extent.width, 1u));
                const auto sampleCountY = std::min<uint32_t>(64u, std::max(debugTexture->extent.height, 1u));
                for (uint32_t sy = 0; sy < sampleCountY; ++sy)
                {
                    const auto y =
                        std::min(debugTexture->extent.height - 1u,
                                 static_cast<uint32_t>((static_cast<uint64_t>(sy) * debugTexture->extent.height) /
                                                       sampleCountY));
                    for (uint32_t sx = 0; sx < sampleCountX; ++sx)
                    {
                        const auto x =
                            std::min(debugTexture->extent.width - 1u,
                                     static_cast<uint32_t>((static_cast<uint64_t>(sx) * debugTexture->extent.width) /
                                                           sampleCountX));
                        const auto offset = (static_cast<uint64_t>(y) * debugTexture->extent.width + x) * 4u;
                        if (offset + 2u >= pixels->size())
                            continue;
                        const float r            = static_cast<float>((*pixels)[offset + 0u]) / 255.0f;
                        const float g            = static_cast<float>((*pixels)[offset + 1u]) / 255.0f;
                        const float b            = static_cast<float>((*pixels)[offset + 2u]) / 255.0f;
                        const float displayValue = (m_RuntimeTexturePreviewMode == 0) ? ((r + g + b) / 3.0f) : r;
                        if (displayValue <= 0.001f || displayValue >= 0.999f)
                            continue;
                        minValue = std::min(minValue, displayValue);
                        maxValue = std::max(maxValue, displayValue);
                        found    = true;
                    }
                }

                if (!found)
                    return false;

                const float oldMin = m_RuntimeTexturePreviewClampMin;
                const float oldRange =
                    std::max(m_RuntimeTexturePreviewClampMax - m_RuntimeTexturePreviewClampMin, 0.0001f);
                const float padding             = std::max((maxValue - minValue) * 0.08f, 1.0f / 255.0f);
                const float low                 = std::max(0.0f, minValue - padding);
                const float high                = std::min(1.0f, maxValue + padding);
                m_RuntimeTexturePreviewClampMin = oldMin + low * oldRange;
                m_RuntimeTexturePreviewClampMax = oldMin + high * oldRange;
                ui::normalizePreviewClamp(m_RuntimeTexturePreviewClampMin, m_RuntimeTexturePreviewClampMax);
                return true;
            };
            const auto frame               = static_cast<uint64_t>(ImGui::GetFrameCount());
            const bool pendingAutoFitReady = m_PendingRuntimeTexturePreviewAutoFitKey == debugTexture->resourceKey &&
                                             debugTexture->texture &&
                                             frame >= m_PendingRuntimeTexturePreviewAutoFitNextTryFrame &&
                                             (debugTexture->texture != m_PendingRuntimeTexturePreviewAutoFitTexture ||
                                              frame >= m_PendingRuntimeTexturePreviewAutoFitFrame);
            if (pendingAutoFitReady)
            {
                if (autoFitClamp())
                {
                    clearPendingAutoFit();
                }
                else if (frame < m_PendingRuntimeTexturePreviewAutoFitDeadlineFrame)
                {
                    m_PendingRuntimeTexturePreviewAutoFitNextTryFrame = frame + 6u;
                }
                else
                {
                    clearPendingAutoFit();
                }
            }

            const float sourceW = static_cast<float>(std::max(debugTexture->sourceExtent.width, 1u));
            const float sourceH = static_cast<float>(std::max(debugTexture->sourceExtent.height, 1u));
            ImGui::BeginChild("##RuntimeTexturePreviewImage",
                              ImGui::GetContentRegionAvail(),
                              true,
                              ImGuiWindowFlags_HorizontalScrollbar);
            const ImVec2 avail          = ImGui::GetContentRegionAvail();
            const float  fitSourceW     = showStereoPreview ? sourceW * 2.0f + ImGui::GetStyle().ItemSpacing.x : sourceW;
            const float  fitSourceH     = showStereoPreview ? sourceH + ImGui::GetTextLineHeightWithSpacing() : sourceH;
            const float  fitScale       = ui::computeTextureFitScale(avail, fitSourceW, fitSourceH);
            const float  effectiveScale = m_RuntimeTexturePreviewAutoFit ? fitScale : m_RuntimeTexturePreviewScale;
            const ImVec2 imageSize {sourceW * effectiveScale, sourceH * effectiveScale};
            if (m_RuntimeTexturePreviewAutoFit)
            {
                const float totalWidth =
                    showStereoPreview ? imageSize.x * 2.0f + ImGui::GetStyle().ItemSpacing.x : imageSize.x;
                const float totalHeight =
                    showStereoPreview ? imageSize.y + ImGui::GetTextLineHeightWithSpacing() : imageSize.y;
                if (totalWidth < avail.x)
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail.x - totalWidth) * 0.5f);
                if (totalHeight < avail.y)
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (avail.y - totalHeight) * 0.5f);
            }
            if (showStereoPreview)
            {
                ImGui::BeginGroup();
                ImGui::TextDisabled("Left Eye");
                ImGui::Image(leftTextureId, imageSize);
                (void)ui::capturePreviewItemInput();
                ImGui::EndGroup();
                ImGui::SameLine();
                ImGui::BeginGroup();
                ImGui::TextDisabled("Right Eye");
                ImGui::Image(rightTextureId, imageSize);
                (void)ui::capturePreviewItemInput();
                ImGui::EndGroup();
            }
            else
            {
                ImGui::Image(primaryTextureId, imageSize);
                (void)ui::capturePreviewItemInput();
            }
            ui::capturePreviewInput(ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem));
            ImGui::EndChild();
            ImGui::EndPopup();
        }
        if (!m_RuntimeTexturePreviewOpen && !m_RuntimeTexturePreviewOverrideKey.empty())
        {
            restoreRuntimeTexturePreviewOverrides();
        }
        if (!m_RuntimeTexturePreviewOpen)
            m_RuntimeTexturePreviewPopupPendingOpen = false;
    }

    void RenderGraphWindow::drawGraphEditor(EditorContext& ctx)
    {
        const EditorCpuScope perf {ctx, "Editor::RenderGraph/GraphEditor"};
        if (!m_GraphEditor)
            m_GraphEditor = std::make_unique<GraphEditorState>();

        auto& state = *m_GraphEditor;
        const auto currentProject = ctx.state.currentProject.lexically_normal();
        if (m_RenderGraphPassCatalogProject != currentProject ||
            m_RenderGraphPassCatalogAssetRoot != ctx.state.currentAssetRoot ||
            m_RenderGraphPassCatalogAssetGeneration != ctx.state.assetFileGeneration)
        {
            registerEditorProjectRenderGraphPasses(ctx, state.registry);
            m_RenderGraphPassCatalogProject           = currentProject;
            m_RenderGraphPassCatalogAssetRoot         = ctx.state.currentAssetRoot;
            m_RenderGraphPassCatalogAssetGeneration   = ctx.state.assetFileGeneration;
        }
        const bool switchedGraph = drawProjectRenderGraphSelector(m_RenderGraphAssetUris,
                                                                  m_RenderGraphAssetProject,
                                                                  m_RenderGraphAssetRoot,
                                                                  m_RenderGraphAssetGeneration,
                                                                  ctx,
                                                                  state.status);
        if (switchedGraph)
        {
            if (auto* renderService = ctx.services ? ctx.services->tryGet<vultra::IRenderService>() : nullptr)
            {
                renderService->setFrameGraphTextureCaptureEnabled(false);
                renderService->clearFrameGraphTexturePreviewOverrides();
            }
            m_RuntimeGraph.reset();
            m_RuntimeGraphPopupOpen     = false;
            m_RuntimeGraphCleanupPending = false;
            m_RuntimeTexturePreviewOpen = false;
            m_RuntimeTexturePreviewPopupPendingOpen = false;
            m_RuntimeGraphTextureCaptureReadyFrame = 0u;
            m_RuntimeTexturePreviewKey.clear();
            m_RuntimeTexturePreviewTitle.clear();
            m_RuntimeTexturePreviewDefaultsKey.clear();
            m_RuntimeTexturePreviewOverrideKey.clear();
            m_RuntimeTexturePreviewOverrideKeys.clear();
            m_PendingRuntimeTexturePreviewAutoFitKey.clear();
            m_PendingRuntimeTexturePreviewAutoFitTexture       = nullptr;
            m_PendingRuntimeTexturePreviewAutoFitFrame         = 0u;
            m_PendingRuntimeTexturePreviewAutoFitDeadlineFrame = 0u;
            m_PendingRuntimeTexturePreviewAutoFitNextTryFrame  = 0u;
            m_RuntimeGraphTextureAutoFitDone.clear();
            m_RuntimeGraphTextureDefaultPreviewDone.clear();
            m_RuntimeGraphTexturePreviewSettings.clear();
            m_RuntimeGraphTextureAutoFitNextFrame.clear();
            m_RuntimeGraphTextureAutoFitDeadlineFrame.clear();
            releaseOverlayRenderTarget(ctx);

            state.loaded        = false;
            state.dirty         = false;
            state.pipelineDirty = false;
            state.runtimeDirty  = false;
            state.path.clear();
            state.loadedUri.clear();
            state.pipelinePath.clear();
            state.editingFeatureInternals = true;
            state.editingFeature.clear();
            state.builtinGraphAsset = false;
        }
        ImGui::Separator();

        const bool isBuiltinGraph = ctx.state.currentEditingRenderGraph.starts_with("builtin://render/");
        const auto graphPath = !isBuiltinGraph && ctx.state.currentEditingRenderGraph.ends_with(".vrg.json") ?
                                   assetPathForUri(ctx, ctx.state.currentEditingRenderGraph) :
                                   std::filesystem::path {};
        auto       graphUri  = [&]() {
            if (ctx.state.currentEditingRenderGraph.starts_with("builtin://render/"))
                return ctx.state.currentEditingRenderGraph;
            if (ctx.state.currentProject.empty() || state.path.empty())
                return std::string {};
            std::error_code ec;
            const auto      assetRoot = (ctx.state.currentProject / ctx.state.currentAssetRoot).lexically_normal();
            const auto      rel = std::filesystem::relative(state.path, assetRoot, ec);
            if (ec || rel.empty())
                return std::string {};
            return "res://" + rel.generic_string();
        };
        auto serializedGraph = [&]() {
            state.storeMeta();
            removeDefaultOutputRefs(state.graph);
            return vrendergraph::saveRenderGraph(state.graph).dump(2);
        };
        auto applyGraphToRuntime = [&]() {
            const auto uri = graphUri();
            if (uri.empty())
                return false;

            std::string validationError;
            if (!validateRenderGraph(state.registry, state.graph, validationError))
            {
                state.status            = "Validation failed: " + validationError;
                ctx.state.statusMessage = state.status;
                return false;
            }

            auto* assetService  = ctx.services ? ctx.services->tryGet<vultra::IAssetService>() : nullptr;
            auto* renderService = ctx.services ? ctx.services->tryGet<vultra::IRenderService>() : nullptr;
            if (!assetService || !renderService)
            {
                state.status = "Runtime apply failed: render/asset service unavailable.";
                return false;
            }

            assetService->setTextAssetOverride(uri, serializedGraph());
            if (!renderService->updateRenderGraph(uri, rendererKeyFromRenderGraphUri(uri)))
                renderService->reloadRenderPipeline(uri, rendererKeyFromRenderGraphUri(uri));
            state.runtimeDirty      = false;
            state.status            = "Applied in memory";
            ctx.state.statusMessage = "Applied render graph in memory: " + uri;
            return true;
        };
        auto persistGraph = [&]() {
            if (state.builtinGraphAsset)
            {
                state.status            = "Builtin graphs are read-only; use Export.";
                ctx.state.statusMessage = state.status;
                return false;
            }

            std::string validationError;
            if (!validateRenderGraph(state.registry, state.graph, validationError))
            {
                state.status            = "Validation failed: " + validationError;
                ctx.state.statusMessage = state.status;
                return false;
            }

            const auto  text = serializedGraph();
            std::string error;

            if (!writeTextAtomic(state.path, text, error))
            {
                state.status            = error;
                ctx.state.statusMessage = error;
                return false;
            }

            state.dirty             = false;
            state.pipelineDirty     = false;
            state.runtimeDirty      = false;
            state.status            = "Saved";
            ctx.state.statusMessage = "Saved render graph: " + state.path.generic_string();

            if (auto* assetService = ctx.services ? ctx.services->tryGet<vultra::IAssetService>() : nullptr)
            {
                const auto uri = graphUri();
                if (!uri.empty())
                    assetService->clearTextAssetOverride(uri);
            }
            if (auto* renderService = ctx.services ? ctx.services->tryGet<vultra::IRenderService>() : nullptr)
            {
                const auto uri = graphUri();
                if (!uri.empty())
                {
                    if (!renderService->updateRenderGraph(uri, rendererKeyFromRenderGraphUri(uri)))
                        renderService->reloadRenderPipeline(uri, rendererKeyFromRenderGraphUri(uri));
                }
            }
            return true;
        };

        if (isBuiltinGraph && ctx.state.currentEditingRenderGraph != state.loadedUri)
        {
            state.path.clear();
            state.loadedUri              = ctx.state.currentEditingRenderGraph;
            state.pipelinePath.clear();
            state.directGraphAsset        = true;
            state.builtinGraphAsset       = true;
            state.loaded                  = false;
            state.dirty                   = false;
            state.pipelineDirty           = false;
            state.runtimeDirty            = false;
            state.editingFeatureInternals = true;
            state.editingFeature.clear();
            state.status.clear();
        }
        else if (!graphPath.empty() && graphPath != state.path)
        {
            state.path = graphPath;
            state.loadedUri = ctx.state.currentEditingRenderGraph;
            state.pipelinePath.clear();
            state.directGraphAsset        = true;
            state.builtinGraphAsset       = false;
            state.loaded                  = false;
            state.dirty                   = false;
            state.pipelineDirty           = false;
            state.runtimeDirty            = false;
            state.editingFeatureInternals = true;
            state.editingFeature.clear();
            state.status.clear();
        }

        if (ImGui::Button(ICON_MDI_PLUS " Add"))
        {
            const ImVec2 buttonMin = ImGui::GetItemRectMin();
            const ImVec2 buttonMax = ImGui::GetItemRectMax();
            state.addMenuScreenPos = ImVec2 {buttonMin.x, buttonMax.y + ImGui::GetStyle().ItemSpacing.y};
            state.hasAddMenuScreenPos = true;
            ImGui::OpenPopup("RenderGraphAddMenu");
        }
        drawGraphEditorAddPopup(ctx);

        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_MDI_REFRESH " Reload"))
        {
            if (auto* assetService = ctx.services ? ctx.services->tryGet<vultra::IAssetService>() : nullptr)
            {
                const auto uri = graphUri();
                if (!uri.empty())
                    assetService->clearTextAssetOverride(uri);
            }
            state.loaded = false;
        }
        ImGui::SameLine();
        ImGui::Checkbox("Live Apply", &state.liveApply);
        ImGui::SameLine();

        const bool canSave = state.loaded && !state.path.empty() && !state.builtinGraphAsset;
        if (!canSave)
            ImGui::BeginDisabled();
        if (ImGui::SmallButton(ICON_MDI_CONTENT_SAVE " Save"))
        {
            persistGraph();
        }
        if (!canSave)
            ImGui::EndDisabled();

        ImGui::SameLine();
        const bool canExport = state.loaded && state.builtinGraphAsset;
        if (!canExport)
            ImGui::BeginDisabled();
        if (ImGui::SmallButton(ICON_MDI_EXPORT " Export"))
        {
            m_BuiltinRenderGraphExportPath[0] = '\0';
            ImGui::OpenPopup("Export Builtin Render Graph");
        }
        if (!canExport)
            ImGui::EndDisabled();

        if (ImGui::BeginPopupModal("Export Builtin Render Graph", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("Builtin graphs are read-only. Choose an export target.");
            const auto filename =
                std::filesystem::path(std::string(ctx.state.currentEditingRenderGraph)).filename().generic_string();
            ImGui::TextDisabled("Suggested file: %s", filename.c_str());
            m_BuiltinRenderGraphExportDialog.drawBrowseOnly(
                "Target", m_BuiltinRenderGraphExportPath.data(), m_BuiltinRenderGraphExportPath.size());

            const bool hasTarget = m_BuiltinRenderGraphExportPath[0] != '\0';
            if (!hasTarget)
                ImGui::BeginDisabled();
            if (ImGui::Button(ICON_MDI_EXPORT " Export", ImVec2 {112.0f, 0.0f}))
            {
                std::string error;
                const auto  target = std::filesystem::path(m_BuiltinRenderGraphExportPath.data()).lexically_normal();
                if (writeTextAtomic(target, serializedGraph(), error))
                {
                    state.status            = "Exported";
                    ctx.state.statusMessage = "Exported builtin render graph: " + target.generic_string();
                    ImGui::CloseCurrentPopup();
                }
                else
                {
                    state.status            = error;
                    ctx.state.statusMessage = error;
                }
            }
            if (!hasTarget)
                ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2 {96.0f, 0.0f}))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        ImGui::SameLine();
        if (ImGui::SmallButton("Topo Order") && state.loaded)
        {
            std::string error;
            if (applyTopoOrder(state.graph, &error))
            {
                state.markDirty();
                state.status = "Topo order applied";
            }
            else
            {
                state.status = error;
            }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_MDI_GRAPH " Auto Layout") && state.loaded)
        {
            applyRenderGraphAutoLayout(state.registry, state.graph);
            state.markDirty();
            state.applyPositions = true;
            state.status         = "Auto layout applied";
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_MDI_DELETE " Delete Selected") && state.loaded)
            state.removeSelected();

        const auto graphLabel = state.builtinGraphAsset ? state.loadedUri :
                                state.path.empty()     ? std::string {"No render graph selected"} :
                                                         state.path.generic_string();
        ImGui::TextDisabled("%s%s%s",
                            graphLabel.c_str(),
                            state.builtinGraphAsset ? " (builtin, read-only)" : "",
                            state.dirty ? " *" : "");

        std::string bannerMessage;
        bool        bannerError = false;
        if (state.loaded)
        {
            std::string validationError;
            if (!validateRenderGraph(state.registry, state.graph, validationError))
            {
                bannerMessage = "Graph invalid: " + validationError;
                bannerError   = true;
            }
        }
        if (bannerMessage.empty() && !state.status.empty())
        {
            bannerMessage = state.status;
            bannerError =
                state.status.find("failed") != std::string::npos || state.status.find("Failed") != std::string::npos ||
                state.status.find("error") != std::string::npos || state.status.find("Error") != std::string::npos ||
                state.status.find("Invalid") != std::string::npos || state.status.find("invalid") != std::string::npos;
        }
        drawGraphStatusBanner(bannerMessage, bannerError);

        ImGui::Separator();

        if (!state.loaded && state.builtinGraphAsset)
        {
            if (auto* assetService = ctx.services ? ctx.services->tryGet<vultra::IAssetService>() : nullptr)
            {
                auto text = assetService->loadTextAssetSync(state.loadedUri);
                if (text)
                {
                    try
                    {
                        const auto json = nlohmann::json::parse(text.value());
                        state.graph     = vrendergraph::loadRenderGraph(json);
                        if (repairMissingInputRefs(state.registry, state.graph))
                        {
                            state.dirty        = true;
                            state.runtimeDirty = true;
                            state.status       = "Repaired stale render graph links";
                        }
                        for (auto& pass : state.graph.passes)
                        {
                            if (state.registry.contains(pass.type))
                                ensureSlots(pass, state.registry.get(pass.type));
                        }
                        state.pipelineFeatures.clear();
                        state.pipelineText.clear();
                        state.loaded         = true;
                        state.dirty          = false;
                        state.pipelineDirty  = false;
                        state.runtimeDirty   = false;
                        state.applyPositions = true;
                        state.status         = "Loaded builtin graph";
                    }
                    catch (const std::exception& e)
                    {
                        ImGui::TextColored(
                            ImVec4 {1.0f, 0.35f, 0.25f, 1.0f}, "Failed to load builtin graph: %s", e.what());
                    }
                }
            }
        }

        if (!state.loaded && !state.path.empty())
        {
            std::ifstream file(state.path);
            if (file.is_open())
            {
                try
                {
                    nlohmann::json json;
                    file >> json;
                    state.graph = vrendergraph::loadRenderGraph(json);
                    if (repairMissingInputRefs(state.registry, state.graph))
                    {
                        state.dirty        = true;
                        state.runtimeDirty = true;
                        state.status       = "Repaired stale render graph links";
                    }
                    for (auto& pass : state.graph.passes)
                    {
                        if (state.registry.contains(pass.type))
                            ensureSlots(pass, state.registry.get(pass.type));
                    }
                    state.pipelineFeatures.clear();
                    state.pipelineText.clear();
                    state.loaded         = true;
                    state.dirty          = false;
                    state.pipelineDirty  = false;
                    state.runtimeDirty   = false;
                    state.applyPositions = true;
                    state.status         = "Loaded";
                }
                catch (const std::exception& e)
                {
                    ImGui::TextColored(ImVec4 {1.0f, 0.35f, 0.25f, 1.0f}, "Failed to load graph: %s", e.what());
                }
            }
        }

        if (!state.loaded)
        {
            ImGui::TextDisabled("Open a .vrg.json render graph asset.");
            return;
        }

        bool drawPipeline = !state.editingFeatureInternals && !state.directGraphAsset;
        if (state.editingFeatureInternals)
        {
            if (!state.directGraphAsset && ImGui::SmallButton(ICON_MDI_ARROW_LEFT " Pipeline"))
            {
                state.storeMeta();
                state.editingFeatureInternals = false;
                state.editingFeature.clear();
                state.applyPositions      = true;
                state.focusPipelineEditor = true;
                drawPipeline              = true;
            }
            if (!drawPipeline)
            {
                if (!state.directGraphAsset)
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled("Editing %s", state.editingFeature.c_str());
                }
                drawGraphEditorCanvas(ctx);
            }
        }
        if (drawPipeline)
        {
            drawPipelineEditorCanvas(ctx);
        }

        if (!ImGui::GetIO().WantTextInput && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
            ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S))
            persistGraph();

        if (state.liveApply && state.runtimeDirty && !graphUri().empty())
            applyGraphToRuntime();
    }

    void RenderGraphWindow::drawGraphEditorAddPopup(EditorContext& ctx)
    {
        const EditorCpuScope perf {ctx, "Editor::RenderGraph/AddPopup"};
        auto& state = *m_GraphEditor;

        if (!ImGui::BeginPopup("RenderGraphAddMenu"))
            return;

        const auto addPassItem = [&](const std::string& type) {
            if (!ImGui::MenuItem(type.c_str()))
                return;

            const auto& def    = state.registry.get(type);
            int         suffix = 1;
            std::string id     = type;
            while (findPass(state.graph, id))
                id = type + "_" + std::to_string(suffix++);

            vrendergraph::PassDecl pass;
            pass.id   = std::move(id);
            pass.type = type;
            ensureSlots(pass, def);
            const std::string newPassId = pass.id;
            state.graph.passes.push_back(std::move(pass));
            if (state.hasAddMenuScreenPos)
            {
                state.pendingPlacementNode      = newPassId;
                state.pendingPlacementScreenPos = state.addMenuScreenPos;
                state.hasPendingPlacement       = true;
                state.hasAddMenuScreenPos       = false;
            }
            repairMissingInputRefs(state.registry, state.graph);
            state.markDirty();
            state.applyPositions = true;
            ImGui::CloseCurrentPopup();
        };

        auto projectTypes = listEditorProjectRenderGraphPassTypes(ctx);
        std::erase_if(projectTypes, [&](const std::string& type) { return !state.registry.contains(type); });
        const std::unordered_set<std::string> projectTypeSet(projectTypes.begin(), projectTypes.end());

        if (state.editingFeatureInternals && !projectTypes.empty() && ImGui::BeginMenu("Project Pass"))
        {
            for (const auto& type : projectTypes)
                addPassItem(type);
            ImGui::EndMenu();
        }

        if (state.editingFeatureInternals && ImGui::BeginMenu("Builtin Pass"))
        {
            auto types = state.registry.listTypes();
            std::sort(types.begin(), types.end());
            for (const auto& type : types)
            {
                if (projectTypeSet.contains(type) || type == "DepthPre")
                    continue;
                addPassItem(type);
            }
            ImGui::EndMenu();
        }

        ImGui::EndPopup();
    }

    void RenderGraphWindow::drawPipelineEditorCanvas(EditorContext& ctx)
    {
        const EditorCpuScope perf {ctx, "Editor::RenderGraph/PipelineCanvas"};
        auto& state = *m_GraphEditor;
        state.pins.clear();
        ImNodes::EditorContextSet(state.pipelineEditorContext);

        ImGui::BeginChild("##RenderGraphPipelineEditor",
                          ImVec2(0, 0),
                          true,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        const EditorCpuScope imnodesPerf {ctx, "Editor::RenderGraph/PipelineImNodes"};
        if (state.focusPipelineEditor)
        {
            ImGui::SetNextWindowFocus();
            state.focusPipelineEditor = false;
        }
        ImNodes::BeginNodeEditor();

        for (size_t i = 0; i < state.pipelineFeatures.size(); ++i)
        {
            const auto&       feature       = state.pipelineFeatures[i];
            const std::string nodeKey       = "feature:" + feature;
            const int         id            = state.nodeId("feature", feature);
            const bool        isCustomGraph = state.isCurrentGraphFeature(feature);
            const float       nodeWidth     = std::clamp(
                std::max(textWidth(feature), textWidth("Double-click to edit internals")) + 58.0f, 230.0f, 420.0f);
            pushNodeTitlePalette(isCustomGraph ? IM_COL32(145, 96, 205, 255) : IM_COL32(70, 130, 190, 255));
            ImNodes::BeginNode(id);
            ImNodes::BeginNodeTitleBar();
            drawNodeTitleText(isCustomGraph ? "Custom Render Graph" : feature.c_str());
            ImNodes::EndNodeTitleBar();
            ImGui::TextDisabled("%s", isCustomGraph ? feature.c_str() : "builtin feature");
            if (isCustomGraph)
            {
                ImGui::Separator();
                ImGui::TextDisabled("%zu passes", state.graph.passes.size());
                ImGui::TextDisabled("Double-click to edit internals");
            }

            ImNodes::BeginInputAttribute(state.pinId(nodeKey, "in", true), ImNodesPinShape_TriangleFilled);
            ImGui::TextDisabled("in");
            ImNodes::EndInputAttribute();
            ImNodes::BeginOutputAttribute(state.pinId(nodeKey, "out", false), ImNodesPinShape_TriangleFilled);
            ImGui::Indent(std::max(24.0f, nodeWidth - textWidth("out") - 42.0f));
            ImGui::TextDisabled("out");
            ImNodes::EndOutputAttribute();
            ImNodes::EndNode();
            popNodeTitlePalette();

            if (state.applyPositions)
            {
                if (auto pos = state.readNodePos(nodeKey))
                    ImNodes::SetNodeGridSpacePos(id, *pos);
                else
                    ImNodes::SetNodeGridSpacePos(id, ImVec2 {static_cast<float>(i) * 340.0f, 40.0f});
            }
        }

        for (size_t i = 1; i < state.pipelineFeatures.size(); ++i)
        {
            const std::string from = "feature:" + state.pipelineFeatures[i - 1];
            const std::string to   = "feature:" + state.pipelineFeatures[i];
            ImNodes::Link(state.pipelineLinkId(from, to), state.pinId(from, "out", false), state.pinId(to, "in", true));
        }

        ImNodes::MiniMap(0.2f, ImNodesMiniMapLocation_BottomRight);
        ImNodes::EndNodeEditor();
        state.applyPositions = false;

        int start = 0;
        int end   = 0;
        if (ImNodes::IsLinkCreated(&start, &end))
        {
            auto fromIt = state.pins.find(start);
            auto toIt   = state.pins.find(end);
            if (fromIt != state.pins.end() && toIt != state.pins.end())
            {
                auto from = fromIt->second;
                auto to   = toIt->second;
                if (from.input && !to.input)
                    std::swap(from, to);
                if (!from.input && to.input && from.node.starts_with("feature:") && to.node.starts_with("feature:"))
                {
                    state.moveFeatureAfter(to.node.substr(8), from.node.substr(8));
                }
                else
                {
                    state.status = "Connect feature output to another feature input to reorder the pipeline.";
                }
            }
        }

        const bool canvasFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        const bool canvasHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows |
                                                          ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

        if (canvasFocused && ImGui::IsKeyPressed(ImGuiKey_Delete))
        {
            const int selectedNodes = ImNodes::NumSelectedNodes();
            if (selectedNodes > 0)
            {
                std::vector<int> nodes(static_cast<size_t>(selectedNodes));
                ImNodes::GetSelectedNodes(nodes.data());
                for (const int node : nodes)
                {
                    std::string feature;
                    if (!state.isFeatureNode(node, &feature))
                        continue;
                    state.pipelineFeatures.erase(
                        std::remove(state.pipelineFeatures.begin(), state.pipelineFeatures.end(), feature),
                        state.pipelineFeatures.end());
                    state.markPipelineDirty();
                    state.status = "Deleted pipeline feature";
                }
                ImNodes::ClearNodeSelection();
            }
        }

        int        hoveredNode = 0;
        const bool nodeHovered = ImNodes::IsNodeHovered(&hoveredNode);
        if (nodeHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            std::string feature;
            if (state.isFeatureNode(hoveredNode, &feature) && state.isCurrentGraphFeature(feature))
            {
                state.storeMeta();
                state.editingFeatureInternals = true;
                state.editingFeature          = feature;
                state.applyPositions          = true;
            }
        }

        if (canvasHovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
        {
            if (nodeHovered)
            {
                state.contextNode = hoveredNode;
                ImGui::OpenPopup("RenderGraphPipelineNodeMenu");
            }
            else
            {
                state.addMenuScreenPos = ImGui::GetMousePos();
                state.hasAddMenuScreenPos = true;
                ImGui::OpenPopup("RenderGraphAddMenu");
            }
        }

        if (ImGui::BeginPopup("RenderGraphPipelineNodeMenu"))
        {
            std::string feature;
            if (state.isFeatureNode(state.contextNode, &feature))
            {
                const bool isCustomGraph = state.isCurrentGraphFeature(feature);
                if (!isCustomGraph)
                    ImGui::BeginDisabled();
                if (ImGui::MenuItem("Edit Internals"))
                {
                    state.storeMeta();
                    state.editingFeatureInternals = true;
                    state.editingFeature          = feature;
                    state.applyPositions          = true;
                }
                if (!isCustomGraph)
                    ImGui::EndDisabled();
                if (ImGui::MenuItem("Delete Feature"))
                {
                    state.pipelineFeatures.erase(
                        std::remove(state.pipelineFeatures.begin(), state.pipelineFeatures.end(), feature),
                        state.pipelineFeatures.end());
                    state.markPipelineDirty();
                    state.status = "Deleted pipeline feature";
                }
            }
            ImGui::EndPopup();
        }

        drawGraphEditorAddPopup(ctx);

        const ImVec2 childPos  = ImGui::GetWindowPos();
        const ImVec2 childSize = ImGui::GetWindowSize();
        ImGui::EndChild();
        drawGameViewOverlay(ctx, childPos, ImVec2 {childPos.x + childSize.x, childPos.y + childSize.y});
    }

    void RenderGraphWindow::drawGraphEditorCanvas(EditorContext& ctx)
    {
        const EditorCpuScope perf {ctx, "Editor::RenderGraph/Canvas"};
        auto& state = *m_GraphEditor;
        {
            const EditorCpuScope beginPerf {ctx, "Editor::RenderGraph/CanvasBegin"};
            state.pins.clear();
            ImNodes::EditorContextSet(state.editorContext);
        }

        ImGui::BeginChild("##RenderGraphNodeEditor",
                          ImVec2(0, 0),
                          true,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        const EditorCpuScope imnodesPerf {ctx, "Editor::RenderGraph/CanvasImNodes"};
        ImNodes::BeginNodeEditor();

        size_t passIndex = 0;
        std::unordered_set<std::string> inputResources;
        for (const auto& pass : state.graph.passes)
        {
            for (const auto& [_, ref] : pass.inputs)
            {
                auto parsed = parseResRef(ref);
                if (parsed && hasResource(state.graph, parsed->node))
                    inputResources.insert(parsed->node);
            }
        }
        {
            const EditorCpuScope resourceNodesPerf {ctx, "Editor::RenderGraph/CanvasResourceNodes"};
            size_t               resourceIndex = 0;
            for (const auto& resource : state.graph.resources)
            {
                if (!inputResources.contains(resource.name))
                    continue;

                const int id = state.nodeId("resource_in", resource.name);
                pushNodeTitlePalette(IM_COL32(57, 139, 183, 255));
                ImNodes::BeginNode(id);
                ImNodes::BeginNodeTitleBar();
                drawNodeTitleText(resource.name.c_str());
                ImNodes::EndNodeTitleBar();
                ImGui::TextDisabled("imported resource");
                const int pin = state.pinId(resource.name, "out", false);
                ImNodes::BeginOutputAttribute(pin, ImNodesPinShape_CircleFilled);
                ImGui::Indent(std::max(24.0f, 120.0f - textWidth("out") - 42.0f));
                ImGui::TextUnformatted("out");
                ImNodes::EndOutputAttribute();
                ImNodes::EndNode();
                popNodeTitlePalette();

                if (state.applyPositions)
                {
                    if (auto pos = state.readNodePos(state.metaKeyForResource(resource.name)))
                        ImNodes::SetNodeGridSpacePos(id, *pos);
                    else
                        ImNodes::SetNodeGridSpacePos(
                            id, ImVec2 {80.0f, 80.0f + static_cast<float>(resourceIndex) * 110.0f});
                }
                ++resourceIndex;
            }
        }
        {
            const EditorCpuScope nodesPerf {ctx, "Editor::RenderGraph/CanvasPassNodes"};
            for (auto& pass : state.graph.passes)
            {
                if (!state.registry.contains(pass.type))
                    continue;

                const auto& def = state.registry.get(pass.type);
                {
                    const EditorCpuScope defaultsPerf {ctx, "Editor::RenderGraph/CanvasEnsureDefaults"};
                    ensureSlots(pass, def);
                    ensureParamDefaults(pass, def);
                }

                const int id = state.nodeId("pass", pass.id);
                float     nodeWidth {250.0f};
                {
                    const EditorCpuScope widthPerf {ctx, "Editor::RenderGraph/CanvasPassNodeWidth"};
                    nodeWidth = passNodeWidth(ctx, pass, def);
                }
                const float paramLabelWidth = std::clamp(nodeWidth * 0.38f, 92.0f, 190.0f);
                const float paramValueWidth = std::clamp(nodeWidth - paramLabelWidth - 58.0f, 150.0f, 300.0f);
                const ImU32 passTitle       = vrgNodeColorFromType(pass.type, false);
                pushNodeTitlePalette(passTitle);
                ImNodes::BeginNode(id);
                ImNodes::BeginNodeTitleBar();
                drawNodeTitleText(pass.id.c_str());
                ImNodes::EndNodeTitleBar();
                ImGui::TextDisabled("%s", pass.type.c_str());

                bool enabled = pass.enabled;
                if (ImGui::Checkbox("Enabled", &enabled))
                {
                    pass.enabled = enabled;
                    state.markDirty();
                }

                {
                    const EditorCpuScope shaderLabelPerf {ctx, "Editor::RenderGraph/CanvasShaderLabel"};
                    if (const auto shaderRef = resolvePassShaderRef(ctx, pass);
                        shaderRef && !primaryShaderId(*shaderRef).empty())
                    {
                        ImGui::TextDisabled("shader: %s", primaryShaderId(*shaderRef).c_str());
                    }
                }

                auto params = def.params;
                {
                    const EditorCpuScope paramsPerf {ctx, "Editor::RenderGraph/CanvasShaderParams"};
                    for (const auto& param : readShaderParamDescs(ctx, pass))
                    {
                        if (std::find_if(params.begin(), params.end(), [&](const auto& existing) {
                                return existing.name == param.name;
                            }) == params.end())
                            params.push_back(param);
                    }
                }
                {
                    const EditorCpuScope paramsUiPerf {ctx, "Editor::RenderGraph/CanvasParamFields"};
                    for (const auto& param : params)
                    {
                        bool paramDirty = false;
                        drawParamField(ctx, pass, param, paramDirty, paramLabelWidth, paramValueWidth);
                        if (paramDirty)
                            state.markDirty();
                    }
                }
                {
                    const EditorCpuScope pinsPerf {ctx, "Editor::RenderGraph/CanvasPins"};
                    if (!def.inputs.empty())
                        ImGui::Spacing();
                    for (const auto& slot : def.inputs)
                    {
                        const int pin = state.pinId(pass.id, slot, true);
                        ImNodes::BeginInputAttribute(pin, ImNodesPinShape_CircleFilled);
                        ImGui::TextUnformatted(slot.c_str());
                        ImNodes::EndInputAttribute();
                    }

                    if (!def.outputs.empty())
                        ImGui::Spacing();
                    for (const auto& slot : def.outputs)
                    {
                        if (!isEditorVisiblePassPin(pass, slot, false))
                            continue;

                        const int pin = state.pinId(pass.id, slot, false);
                        ImNodes::BeginOutputAttribute(pin, ImNodesPinShape_CircleFilled);
                        ImGui::Indent(std::max(24.0f, nodeWidth - textWidth(slot) - 42.0f));
                        ImGui::TextUnformatted(slot.c_str());
                        ImNodes::EndOutputAttribute();
                    }
                }

                ImNodes::EndNode();
                popNodeTitlePalette();

                if (state.applyPositions)
                {
                    const EditorCpuScope positionsPerf {ctx, "Editor::RenderGraph/CanvasApplyPositions"};
                    if (state.hasPendingPlacement && state.pendingPlacementNode == pass.id)
                    {
                        ImNodes::SetNodeScreenSpacePos(id, state.pendingPlacementScreenPos);
                        state.storeNodePos(pass.id, id);
                        state.pendingPlacementNode.clear();
                        state.hasPendingPlacement = false;
                    }
                    else if (auto pos = state.readNodePos(pass.id))
                        ImNodes::SetNodeGridSpacePos(id, *pos);
                    else
                        ImNodes::SetNodeGridSpacePos(
                            id, ImVec2 {680.0f + static_cast<float>(passIndex) * 300.0f, 80.0f});
                }
                ++passIndex;
            }
        }

        {
            const EditorCpuScope linksPerf {ctx, "Editor::RenderGraph/CanvasLinks"};
            for (const auto& pass : state.graph.passes)
            {
                for (const auto& [slot, ref] : pass.inputs)
                {
                    auto parsed = parseResRef(ref);
                    if (!parsed)
                        continue;
                    if (!findPass(state.graph, parsed->node) && !hasResource(state.graph, parsed->node))
                        continue;
                    const auto* sourcePass = findPass(state.graph, parsed->node);
                    if (sourcePass && !isEditorVisiblePassPin(*sourcePass, parsed->slot, false))
                        continue;
                    if (!isEditorVisiblePassPin(pass, slot, true))
                        continue;

                    const int from = state.pinId(parsed->node, parsed->slot, false);
                    const int to   = state.pinId(pass.id, slot, true);
                    ImNodes::Link(state.linkId(ref, pass.id, slot), from, to);
                }
            }
        }

        {
            const EditorCpuScope endPerf {ctx, "Editor::RenderGraph/CanvasMiniMapEnd"};
            ImNodes::MiniMap(0.2f, ImNodesMiniMapLocation_BottomRight);
            ImNodes::EndNodeEditor();
            state.applyPositions = false;
        }

        {
            const EditorCpuScope interactionsPerf {ctx, "Editor::RenderGraph/CanvasInteractions"};
            int                  start = 0;
            int                  end   = 0;
            if (ImNodes::IsLinkCreated(&start, &end))
            {
                auto fromIt = state.pins.find(start);
                auto toIt   = state.pins.find(end);
                if (fromIt != state.pins.end() && toIt != state.pins.end())
                {
                    auto from = fromIt->second;
                    auto to   = toIt->second;
                    if (from.input && !to.input)
                        std::swap(from, to);

                    if (!from.input && to.input)
                    {
                        if (auto* dst = findPass(state.graph, to.node))
                        {
                            dst->inputs[to.slot] = makeResRef(from.node, from.slot);
                            std::string topoError;
                            if (!applyTopoOrder(state.graph, &topoError))
                                state.status = topoError;
                            else
                                state.status = "Updated pass input link";
                            state.markDirty();
                        }
                    }
                }
            }

            int destroyedLink = 0;
            if (ImNodes::IsLinkDestroyed(&destroyedLink))
                state.removeEditableLink(destroyedLink);

            const bool canvasFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
            const bool canvasHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows |
                                                              ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

            if (canvasFocused && ImGui::IsKeyPressed(ImGuiKey_Delete))
                state.removeSelected();

            int        hoveredNode = 0;
            int        hoveredLink = 0;
            const bool nodeHovered = ImNodes::IsNodeHovered(&hoveredNode);
            const bool linkHovered = ImNodes::IsLinkHovered(&hoveredLink);
            if (canvasHovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
            {
                if (linkHovered)
                {
                    state.contextLink = hoveredLink;
                    ImGui::OpenPopup("RenderGraphLinkMenu");
                }
                else if (nodeHovered)
                {
                    state.contextNode = hoveredNode;
                    ImGui::OpenPopup("RenderGraphNodeMenu");
                }
                else
                {
                    state.addMenuScreenPos = ImGui::GetMousePos();
                    state.hasAddMenuScreenPos = true;
                    ImGui::OpenPopup("RenderGraphAddMenu");
                }
            }
        }

        {
            const EditorCpuScope popupsPerf {ctx, "Editor::RenderGraph/CanvasContextPopups"};
            if (ImGui::BeginPopup("RenderGraphLinkMenu"))
            {
                if (ImGui::MenuItem("Delete Link"))
                    state.removeEditableLink(state.contextLink);
                ImGui::EndPopup();
            }

            if (ImGui::BeginPopup("RenderGraphNodeMenu"))
            {
                bool handled = false;
                for (auto it = state.graph.passes.begin(); it != state.graph.passes.end(); ++it)
                {
                    if (state.nodeId("pass", it->id) != state.contextNode)
                        continue;
                    if (ImGui::MenuItem("Enabled", nullptr, it->enabled))
                    {
                        it->enabled = !it->enabled;
                        state.markDirty();
                    }
                    if (ImGui::MenuItem("Delete Pass"))
                        state.removeEditableNode(state.contextNode);
                    handled = true;
                    break;
                }

                ImGui::EndPopup();
            }
        }

        drawGraphEditorAddPopup(ctx);

        const ImVec2 childPos  = ImGui::GetWindowPos();
        const ImVec2 childSize = ImGui::GetWindowSize();
        ImGui::EndChild();
        drawGameViewOverlay(ctx, childPos, ImVec2 {childPos.x + childSize.x, childPos.y + childSize.y});
    }

    bool RenderGraphWindow::ensureRenderGraphPreviewCamera(EditorContext& ctx, uint32_t width, uint32_t height)
    {
        if (ctx.state.gameViewVisibleLastFrame)
        {
            releaseOverlayRenderTarget(ctx);
            return false;
        }

        const uint32_t renderWidth  = std::max(width, 1u);
        const uint32_t renderHeight = std::max(height, 1u);
        const float    aspect       = static_cast<float>(renderWidth) / static_cast<float>(renderHeight);
        const bool     xrPreview    = isXrRenderGraphUri(ctx.state.currentEditingRenderGraph);
        ensureOverlayRenderTarget(ctx, renderWidth, renderHeight, xrPreview ? 2u : 1u);

        vultra::rhi::Texture* renderTarget =
            m_OverlayPendingRenderTarget.texture ? &*m_OverlayPendingRenderTarget.texture :
            m_OverlayActiveRenderTarget.texture  ? &*m_OverlayActiveRenderTarget.texture :
                                                   nullptr;

        bool hasPrimaryCamera = false;
        if (ctx.services && renderTarget)
        {
            if (auto* worldService = ctx.services->tryGet<vultra::IWorldService>())
            {
                auto& world      = worldService->world();
                auto  camera     = findPrimaryCamera(world);
                hasPrimaryCamera = camera != entt::null;
                if (hasPrimaryCamera)
                {
                    if (auto* cameraService = ctx.services->tryGet<vultra::ICameraService>())
                    {
                        cameraService->addManualCamera(makeRenderGraphPreviewCamera(
                            world,
                            camera,
                            aspect,
                            renderTarget,
                            rendererKeyFromRenderGraphUri(ctx.state.currentEditingRenderGraph)));
                    }
                }
            }
        }
        return hasPrimaryCamera;
    }

    void RenderGraphWindow::drawGameViewOverlay(EditorContext& ctx, const ImVec2 childMin, const ImVec2 childMax)
    {
        const EditorCpuScope perf {ctx, "Editor::RenderGraph/GamePreviewOverlay"};
        if (m_RuntimeGraphPopupOpen)
            return;
        if (ctx.state.gameViewVisibleLastFrame)
        {
            releaseOverlayRenderTarget(ctx);
            return;
        }
        const bool anyPopupOpen = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup);

        const ImVec2 childSize {childMax.x - childMin.x, childMax.y - childMin.y};
        if (childSize.x < 220.0f || childSize.y < 160.0f)
            return;

        const bool     xrPreview    = isXrRenderGraphUri(ctx.state.currentEditingRenderGraph);
        const float    aspect       = xrPreview ? kRenderGraphPreviewAspect * 2.0f : kRenderGraphPreviewAspect;
        m_OverlayZoom               = std::clamp(m_OverlayZoom, kOverlayZoomMin, kOverlayZoomMax);
        const float baseWidth       = std::min(320.0f, std::max(180.0f, childSize.x * 0.22f));
        const float width           = std::min(childSize.x - 32.0f, baseWidth * m_OverlayZoom);
        const float height          = width / aspect;
        const auto   renderWidth     = static_cast<uint32_t>(std::max(1.0f, std::round(xrPreview ? width * 0.5f : width)));
        const auto   renderHeight    = static_cast<uint32_t>(std::max(1.0f, std::round(height)));
        if (xrPreview)
        {
            if (auto* renderService = ctx.services ? ctx.services->tryGet<vultra::IRenderService>() : nullptr)
            {
                bool       defaultChannels[4] {true, true, true, true};
                const auto selectedTextureKey =
                    std::string(vultra::FrameGraphTexturePreviewSettings::kCaptureAllTextures);
                const auto previewSettings = ui::makeFrameGraphTexturePreviewSettings(
                    selectedTextureKey, true, defaultChannels, 0, 0.1f, 1000.0f, 0.0f, 1.0f, 0u);
                renderService->setFrameGraphTextureCaptureEnabled(true);
                renderService->setFrameGraphTexturePreviewSettings(previewSettings);
            }
        }
        const bool   hasPrimaryCamera = ensureRenderGraphPreviewCamera(ctx, renderWidth, renderHeight);

        const ImVec2    padding {14.0f, 14.0f};
        constexpr float controlHeight = 30.0f;
        const ImVec2    panelSize {width + padding.x * 2.0f, height + padding.y * 2.0f + 22.0f + controlHeight};
        const ImVec2    panelMin {childMin.x + 16.0f, childMin.y + childSize.y - panelSize.y - 16.0f};
        const ImVec2    panelMax {panelMin.x + panelSize.x, panelMin.y + panelSize.y};
        const ImVec2    imageMin {panelMin.x + padding.x, panelMin.y + padding.y + 22.0f};
        const ImVec2    imageMax {imageMin.x + width, imageMin.y + height};
        const ImVec2    controlsMin {imageMin.x, imageMax.y + 8.0f};
        const ImVec2    mouse    = ImGui::GetIO().MousePos;
        const auto      contains = [&](const ImVec2& min, const ImVec2& max) {
            return mouse.x >= min.x && mouse.x <= max.x && mouse.y >= min.y && mouse.y <= max.y;
        };
        const ImVec2 minusMin {controlsMin.x, controlsMin.y};
        const ImVec2 minusMax {minusMin.x + 24.0f, minusMin.y + 24.0f};
        const ImVec2 labelMin {minusMax.x + 10.0f, controlsMin.y + 4.0f};
        const ImVec2 plusMin {labelMin.x + 56.0f, controlsMin.y};
        const ImVec2 plusMax {plusMin.x + 24.0f, plusMin.y + 24.0f};

        const bool minusHovered = contains(minusMin, minusMax);
        const bool plusHovered  = contains(plusMin, plusMax);
        if (!anyPopupOpen && (minusHovered || plusHovered) && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            if (minusHovered)
                m_OverlayZoom = std::clamp(m_OverlayZoom - kOverlayZoomStep, kOverlayZoomMin, kOverlayZoomMax);
            else
                m_OverlayZoom = std::clamp(m_OverlayZoom + kOverlayZoomStep, kOverlayZoomMin, kOverlayZoomMax);
            ImGui::SetNextFrameWantCaptureMouse(true);
        }

        ImDrawList* drawList =
            anyPopupOpen ? ImGui::GetWindowDrawList() : ImGui::GetForegroundDrawList(ImGui::GetWindowViewport());
        drawList->PushClipRect(childMin, childMax, true);
        drawList->AddRectFilled(panelMin, panelMax, IM_COL32(10, 14, 18, 255), 7.0f);
        drawList->AddRect(panelMin, panelMax, IM_COL32(68, 86, 105, 255), 7.0f);
        drawList->AddText(ImVec2(panelMin.x + padding.x, panelMin.y + 8.0f), IM_COL32(190, 204, 218, 255), "Preview");
        char zoomLabel[16] {};
        std::snprintf(zoomLabel, sizeof(zoomLabel), "%.0f%%", m_OverlayZoom * 100.0f);
        const ImVec2 zoomSize = ImGui::CalcTextSize(zoomLabel);
        drawList->AddText(
            ImVec2(panelMax.x - padding.x - zoomSize.x, panelMin.y + 8.0f), IM_COL32(126, 142, 158, 255), zoomLabel);

        RuntimeStereoTexturePair previewStereoPair;
        if (xrPreview)
        {
            if (auto* renderService = ctx.services ? ctx.services->tryGet<vultra::IRenderService>() : nullptr)
            {
                const auto rendererKey = rendererKeyFromRenderGraphUri(ctx.state.currentEditingRenderGraph);
                for (const auto& left : renderService->frameGraphDebugTextures())
                {
                    if (!left.texture || left.camera != "Render Graph Preview" || left.renderer != rendererKey ||
                        left.layer != 0u || left.sourceExtent.width != renderWidth ||
                        left.sourceExtent.height != renderHeight)
                    {
                        continue;
                    }
                    for (const auto& right : renderService->frameGraphDebugTextures())
                    {
                        if (!right.texture || right.camera != left.camera || right.renderer != left.renderer ||
                            right.layer != 1u ||
                            textureKeyWithoutLayer(right.resourceKey) != textureKeyWithoutLayer(left.resourceKey))
                        {
                            continue;
                        }
                        previewStereoPair = {.left = &left, .right = &right};
                    }
                }
            }
        }

        if (previewStereoPair.left && previewStereoPair.right && hasPrimaryCamera)
        {
            auto* imguiService = ctx.services ? ctx.services->tryGet<vultra::IImGuiService>() : nullptr;
            const auto ensurePreviewId = [&](const vultra::FrameGraphDebugTexture& texture) {
                auto& cached = m_TextureThumbnailCache[texture.key];
                if (imguiService && cached.texture != texture.texture)
                {
                    if (cached.textureId)
                    {
                        cached.retireFrame =
                            static_cast<uint64_t>(ImGui::GetFrameCount()) + kRenderTargetReleaseDelayFrames;
                        m_RetiredTextureThumbnails.push_back(cached);
                    }
                    cached.texture     = texture.texture;
                    cached.textureId   = imguiService->addTexture(*texture.texture, makeLinearClampSampler(ctx));
                    cached.retireFrame = 0;
                }
                return cached.textureId;
            };
            const ImVec2 split {std::floor((imageMin.x + imageMax.x) * 0.5f), imageMax.y};
            drawList->AddRectFilled(imageMin, imageMax, IM_COL32(0, 0, 0, 255), 3.0f);
            drawList->AddImage(ensurePreviewId(*previewStereoPair.left),
                               imageMin,
                               ImVec2 {split.x - 2.0f, split.y},
                               ImVec2(0.0f, 0.0f),
                               ImVec2(1.0f, 1.0f));
            drawList->AddImage(ensurePreviewId(*previewStereoPair.right),
                               ImVec2 {split.x + 2.0f, imageMin.y},
                               imageMax,
                               ImVec2(0.0f, 0.0f),
                               ImVec2(1.0f, 1.0f));
        }
        else if (m_OverlayActiveRenderTarget.textureId && hasPrimaryCamera && !xrPreview)
        {
            drawList->AddRectFilled(imageMin, imageMax, IM_COL32(0, 0, 0, 255), 3.0f);
            drawList->AddImage(
                m_OverlayActiveRenderTarget.textureId, imageMin, imageMax, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f));
        }
        else
        {
            drawList->AddRectFilled(imageMin, imageMax, IM_COL32(16, 19, 24, 255), 3.0f);
            const char*  label    = hasPrimaryCamera ? "Preparing preview" : "No primary camera";
            const ImVec2 textSize = ImGui::CalcTextSize(label);
            drawList->AddText(
                ImVec2((imageMin.x + imageMax.x - textSize.x) * 0.5f, (imageMin.y + imageMax.y - textSize.y) * 0.5f),
                IM_COL32(140, 152, 166, 255),
                label);
        }
        drawList->AddRect(imageMin, imageMax, IM_COL32(72, 86, 104, 255), 3.0f);
        const auto buttonColor = [](bool hovered) {
            return hovered ? IM_COL32(42, 50, 62, 255) : IM_COL32(26, 31, 39, 255);
        };
        drawList->AddRectFilled(minusMin, minusMax, buttonColor(minusHovered), 5.0f);
        drawList->AddText(
            ImVec2(minusMin.x + 4.0f, minusMin.y + 4.0f), IM_COL32(184, 198, 214, 255), ICON_MDI_MAGNIFY_MINUS);
        drawList->AddText(labelMin, IM_COL32(126, 142, 158, 255), zoomLabel);
        drawList->AddRectFilled(plusMin, plusMax, buttonColor(plusHovered), 5.0f);
        drawList->AddText(
            ImVec2(plusMin.x + 4.0f, plusMin.y + 4.0f), IM_COL32(184, 198, 214, 255), ICON_MDI_MAGNIFY_PLUS);
        drawList->PopClipRect();
    }

    void RenderGraphWindow::ensureOverlayRenderTarget(EditorContext& ctx,
                                                      const uint32_t width,
                                                      const uint32_t height,
                                                      const uint32_t layerCount)
    {
        if (!ctx.services || width == 0u || height == 0u)
            return;

        collectRetiredOverlayRenderTargets(ctx);
        if (m_OverlayPendingRenderTarget.texture &&
            static_cast<uint64_t>(ImGui::GetFrameCount()) > m_OverlayPendingRenderTarget.frameCreated)
            promotePendingOverlayRenderTarget(ctx);

        const auto& currentTarget =
            m_OverlayPendingRenderTarget.texture ? m_OverlayPendingRenderTarget : m_OverlayActiveRenderTarget;
        if (currentTarget.texture && currentTarget.extent.width == width && currentTarget.extent.height == height &&
            currentTarget.layerCount == layerCount && (currentTarget.layerCount > 1u || currentTarget.textureId))
            return;

        if (m_OverlayPendingRenderTarget.texture)
            retireOverlayRenderTarget(m_OverlayPendingRenderTarget);

        auto* backendService = ctx.services->tryGet<vultra::IRenderBackendService>();
        auto* imguiService   = ctx.services->tryGet<vultra::IImGuiService>();
        if (!backendService || !imguiService)
            return;

        auto& rd     = backendService->renderDevice();
        auto  format = backendService->backbuffer().getPixelFormat();
        if (format == vultra::rhi::PixelFormat::eUndefined)
            format = vultra::rhi::PixelFormat::eRGBA8_UNorm;

        m_OverlayPendingRenderTarget.extent = {width, height};
        m_OverlayPendingRenderTarget.layerCount = std::max(layerCount, 1u);
        m_OverlayPendingRenderTarget.texture =
            vultra::rhi::Texture::Builder {}
                .setExtent(m_OverlayPendingRenderTarget.extent)
                .setPixelFormat(format)
                .setNumMipLevels(1)
                .setNumLayers(m_OverlayPendingRenderTarget.layerCount > 1u ?
                                  std::optional {m_OverlayPendingRenderTarget.layerCount} :
                                  std::nullopt)
                .setUsageFlags(vultra::rhi::ImageUsage::eRenderTarget | vultra::rhi::ImageUsage::eSampled)
                .build(rd);
        m_OverlayPendingRenderTarget.textureId = m_OverlayPendingRenderTarget.layerCount > 1u ?
                                                     vultra::IImGuiService::TextureID {} :
                                                     imguiService->addTexture(*m_OverlayPendingRenderTarget.texture);
        m_OverlayPendingRenderTarget.frameCreated = static_cast<uint64_t>(ImGui::GetFrameCount());
        m_OverlayPendingRenderTarget.releaseFrame = 0;
    }

    void RenderGraphWindow::promotePendingOverlayRenderTarget(EditorContext& ctx)
    {
        (void)ctx;
        if (!m_OverlayPendingRenderTarget.texture)
            return;

        retireOverlayRenderTarget(m_OverlayActiveRenderTarget);
        m_OverlayActiveRenderTarget  = std::move(m_OverlayPendingRenderTarget);
        m_OverlayPendingRenderTarget = {};
    }

    void RenderGraphWindow::retireOverlayRenderTarget(RenderTargetSlot& slot)
    {
        if (!slot.texture && !slot.textureId)
            return;

        slot.releaseFrame = static_cast<uint64_t>(ImGui::GetFrameCount()) + kRenderTargetReleaseDelayFrames;
        m_OverlayRetiredRenderTargets.push_back(std::move(slot));
        slot = {};
    }

    void RenderGraphWindow::collectRetiredOverlayRenderTargets(EditorContext& ctx)
    {
        const auto frame        = static_cast<uint64_t>(ImGui::GetFrameCount());
        auto*      imguiService = ctx.services ? ctx.services->tryGet<vultra::IImGuiService>() : nullptr;

        std::size_t out = 0;
        for (auto& slot : m_OverlayRetiredRenderTargets)
        {
            if (frame >= slot.releaseFrame)
            {
                if (imguiService && slot.textureId)
                    imguiService->removeTexture(slot.textureId);
                slot.texture.reset();
            }
            else
            {
                m_OverlayRetiredRenderTargets[out++] = std::move(slot);
            }
        }
        m_OverlayRetiredRenderTargets.resize(out);
    }

    void RenderGraphWindow::releaseOverlayRenderTarget(EditorContext& ctx)
    {
        const bool hasOwnedRenderTargets = m_OverlayActiveRenderTarget.texture || m_OverlayPendingRenderTarget.texture ||
                                           !m_OverlayRetiredRenderTargets.empty();
        if (hasOwnedRenderTargets)
        {
            if (auto* backendService = ctx.services ? ctx.services->tryGet<vultra::IRenderBackendService>() : nullptr)
                backendService->renderDevice().waitIdle();
        }
        if (ctx.services)
        {
            if (auto* imguiService = ctx.services->tryGet<vultra::IImGuiService>())
            {
                if (m_OverlayActiveRenderTarget.textureId)
                    imguiService->removeTexture(m_OverlayActiveRenderTarget.textureId);
                if (m_OverlayPendingRenderTarget.textureId)
                    imguiService->removeTexture(m_OverlayPendingRenderTarget.textureId);
                for (auto& slot : m_OverlayRetiredRenderTargets)
                {
                    if (slot.textureId)
                        imguiService->removeTexture(slot.textureId);
                }
            }
        }
        m_OverlayActiveRenderTarget  = {};
        m_OverlayPendingRenderTarget = {};
        m_OverlayRetiredRenderTargets.clear();
    }

    void RenderGraphWindow::collectRetiredTextureThumbnails(EditorContext& ctx)
    {
        const auto frame        = static_cast<uint64_t>(ImGui::GetFrameCount());
        auto*      imguiService = ctx.services ? ctx.services->tryGet<vultra::IImGuiService>() : nullptr;

        std::size_t out = 0;
        for (auto& entry : m_RetiredTextureThumbnails)
        {
            if (frame >= entry.retireFrame)
            {
                if (imguiService && entry.textureId)
                    imguiService->removeTexture(entry.textureId);
                entry.texture = nullptr;
            }
            else
            {
                m_RetiredTextureThumbnails[out++] = entry;
            }
        }
        m_RetiredTextureThumbnails.resize(out);
    }

    void RenderGraphWindow::releaseTextureThumbnails(EditorContext& ctx)
    {
        if (!m_TextureThumbnailCache.empty() || !m_RetiredTextureThumbnails.empty())
        {
            if (auto* backendService = ctx.services ? ctx.services->tryGet<vultra::IRenderBackendService>() : nullptr)
                backendService->renderDevice().waitIdle();
        }

        auto* imguiService = ctx.services ? ctx.services->tryGet<vultra::IImGuiService>() : nullptr;
        if (imguiService)
        {
            for (auto& [_, entry] : m_TextureThumbnailCache)
            {
                if (entry.textureId)
                    imguiService->removeTexture(entry.textureId);
            }
            for (auto& entry : m_RetiredTextureThumbnails)
            {
                if (entry.textureId)
                    imguiService->removeTexture(entry.textureId);
            }
        }
        m_TextureThumbnailCache.clear();
        m_RetiredTextureThumbnails.clear();
    }

    void RenderGraphWindow::resetOverlayRenderTargetForProject(EditorContext& ctx)
    {
        if (m_ProjectGeneration == ctx.state.projectGeneration)
            return;

        if (auto* renderService = ctx.services ? ctx.services->tryGet<vultra::IRenderService>() : nullptr)
        {
            renderService->setFrameGraphTextureCaptureEnabled(false);
            renderService->clearFrameGraphTexturePreviewOverrides();
        }
        m_RuntimeTexturePreviewOverrideKey.clear();
        m_RuntimeTexturePreviewOverrideKeys.clear();
        m_RuntimeTexturePreviewPopupPendingOpen = false;
        m_RuntimeGraphTextureCaptureReadyFrame = 0u;
        m_RuntimeGraphTextureAutoFitDone.clear();
        m_RuntimeGraphTextureDefaultPreviewDone.clear();
        m_RuntimeGraphTexturePreviewSettings.clear();
        m_RuntimeGraphTextureAutoFitNextFrame.clear();
        m_RuntimeGraphTextureAutoFitDeadlineFrame.clear();
        retireOverlayRenderTarget(m_OverlayActiveRenderTarget);
        retireOverlayRenderTarget(m_OverlayPendingRenderTarget);
        releaseTextureThumbnails(ctx);
        m_ProjectGeneration = ctx.state.projectGeneration;
    }
} // namespace vultra_app
