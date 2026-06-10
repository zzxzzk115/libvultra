#include "editor_app/ui/windows/material_graph_window.hpp"

#include "common/ui_widgets.hpp"
#include "editor_app/asset_thumbnail_service.hpp"
#include "editor_app/ui/graph_layout.hpp"

#include <vultra/core/i18n/i18n.hpp>
#include <vultra/core/rhi/structs/render_device_structs.hpp>
#include <vultra/function/imgui/imgui_dpi.hpp>
#include <vultra/function/services/asset_service.hpp>
#include <vultra/function/services/camera_service.hpp>
#include <vultra/function/services/imgui_service.hpp>
#include <vultra/function/services/render_backend_service.hpp>
#include <vultra/function/services/render_service.hpp>
#include <vultra/function/services/shader_service.hpp>
#include <vultra/function/world/components/entity_status_component.hpp>
#include <vultra/function/world/components/environment_component.hpp>
#include <vultra/function/world/components/id_component.hpp>
#include <vultra/function/world/components/light_component.hpp>
#include <vultra/function/world/components/mesh_component.hpp>
#include <vultra/function/world/components/name_component.hpp>
#include <vultra/function/world/components/transform_component.hpp>

#include <IconsMaterialDesignIcons.h>
#include <glm/common.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <ranges>
#include <span>
#include <sstream>
#include <system_error>

namespace vultra_app
{
    namespace
    {
        constexpr const char* kBlackboardTypeLabels[] = {
            "Bool",
            "Int",
            "Float",
            "Vec2",
            "Vec3",
            "Vec4",
            "Color",
            "Texture2D",
        };

        vultra::material_graph::ValueType blackboardTypeFromIndex(const int index)
        {
            using enum vultra::material_graph::ValueType;
            switch (index)
            {
                case 0:
                    return eBool;
                case 1:
                    return eInt;
                case 3:
                    return eVec2;
                case 4:
                    return eVec3;
                case 5:
                    return eVec4;
                case 6:
                    return eColor;
                case 7:
                    return eTexture2D;
                case 2:
                default:
                    return eFloat;
            }
        }

        int blackboardTypeIndex(const vultra::material_graph::ValueType type)
        {
            using enum vultra::material_graph::ValueType;
            switch (type)
            {
                case eBool:
                    return 0;
                case eInt:
                    return 1;
                case eVec2:
                    return 3;
                case eVec3:
                    return 4;
                case eVec4:
                    return 5;
                case eColor:
                    return 6;
                case eTexture2D:
                    return 7;
                case eFloat:
                default:
                    return 2;
            }
        }

        nlohmann::json defaultBlackboardValue(const vultra::material_graph::ValueType type)
        {
            using enum vultra::material_graph::ValueType;
            switch (type)
            {
                case eBool:
                    return false;
                case eInt:
                    return 0;
                case eVec2:
                    return nlohmann::json::array({0.0f, 0.0f});
                case eVec3:
                    return nlohmann::json::array({0.0f, 0.0f, 0.0f});
                case eVec4:
                    return nlohmann::json::array({0.0f, 0.0f, 0.0f, 0.0f});
                case eColor:
                    return nlohmann::json::array({1.0f, 1.0f, 1.0f, 1.0f});
                case eTexture2D:
                    return "";
                case eFloat:
                default:
                    return 0.0f;
            }
        }

        void normalizeBlackboardDefault(vultra::material_graph::BlackboardParameter& param)
        {
            if (param.defaultValue.is_null())
                param.defaultValue = defaultBlackboardValue(param.type);
        }

        bool inputStringField(const char* label, std::string& value)
        {
            std::array<char, 256> buffer {};
            const auto            count = std::min(buffer.size() - 1, value.size());
            std::memcpy(buffer.data(), value.data(), count);
            buffer[count] = '\0';
            if (!ImGui::InputText(label, buffer.data(), buffer.size()))
                return false;
            value = buffer.data();
            return true;
        }

        bool drawBlackboardDefaultValue(vultra::material_graph::BlackboardParameter& param)
        {
            using enum vultra::material_graph::ValueType;
            normalizeBlackboardDefault(param);
            switch (param.type)
            {
                case eBool: {
                    bool value = param.defaultValue.is_boolean() ? param.defaultValue.get<bool>() : false;
                    if (!ImGui::Checkbox(vultra::trId("materialGraph.blackboard.defaultValue", "Default"), &value))
                        return false;
                    param.defaultValue = value;
                    return true;
                }
                case eInt: {
                    int value = param.defaultValue.is_number_integer() ? param.defaultValue.get<int>() : 0;
                    if (!ImGui::InputInt(vultra::trId("materialGraph.blackboard.defaultValue", "Default"), &value))
                        return false;
                    param.defaultValue = value;
                    return true;
                }
                case eVec2: {
                    float value[2] {
                        param.defaultValue.is_array() && param.defaultValue.size() > 0 ?
                            param.defaultValue[0].get<float>() :
                            0.0f,
                        param.defaultValue.is_array() && param.defaultValue.size() > 1 ?
                            param.defaultValue[1].get<float>() :
                            0.0f,
                    };
                    if (!ImGui::DragFloat2(
                            vultra::trId("materialGraph.blackboard.defaultValue", "Default"), value, 0.01f))
                        return false;
                    param.defaultValue = nlohmann::json::array({value[0], value[1]});
                    return true;
                }
                case eVec3: {
                    float value[3] {
                        param.defaultValue.is_array() && param.defaultValue.size() > 0 ?
                            param.defaultValue[0].get<float>() :
                            0.0f,
                        param.defaultValue.is_array() && param.defaultValue.size() > 1 ?
                            param.defaultValue[1].get<float>() :
                            0.0f,
                        param.defaultValue.is_array() && param.defaultValue.size() > 2 ?
                            param.defaultValue[2].get<float>() :
                            0.0f,
                    };
                    if (!ImGui::DragFloat3(
                            vultra::trId("materialGraph.blackboard.defaultValue", "Default"), value, 0.01f))
                        return false;
                    param.defaultValue = nlohmann::json::array({value[0], value[1], value[2]});
                    return true;
                }
                case eVec4:
                case eColor: {
                    float value[4] {
                        param.defaultValue.is_array() && param.defaultValue.size() > 0 ?
                            param.defaultValue[0].get<float>() :
                            1.0f,
                        param.defaultValue.is_array() && param.defaultValue.size() > 1 ?
                            param.defaultValue[1].get<float>() :
                            1.0f,
                        param.defaultValue.is_array() && param.defaultValue.size() > 2 ?
                            param.defaultValue[2].get<float>() :
                            1.0f,
                        param.defaultValue.is_array() && param.defaultValue.size() > 3 ?
                            param.defaultValue[3].get<float>() :
                            1.0f,
                    };
                    const bool changed =
                        param.type == eColor ?
                            ImGui::ColorEdit4(vultra::trId("materialGraph.blackboard.defaultValue", "Default"), value) :
                            ImGui::DragFloat4(
                                vultra::trId("materialGraph.blackboard.defaultValue", "Default"), value, 0.01f);
                    if (!changed)
                        return false;
                    param.defaultValue = nlohmann::json::array({value[0], value[1], value[2], value[3]});
                    return true;
                }
                case eTexture2D: {
                    std::string value =
                        param.defaultValue.is_string() ? param.defaultValue.get<std::string>() : std::string {};
                    if (!inputStringField(vultra::trId("materialGraph.blackboard.defaultUri", "Default URI"), value))
                        return false;
                    param.defaultValue = value;
                    return true;
                }
                case eFloat:
                default: {
                    float      value = param.defaultValue.is_number() ? param.defaultValue.get<float>() : 0.0f;
                    const bool changed =
                        param.hasUiRange ?
                            ImGui::SliderFloat(vultra::trId("materialGraph.blackboard.defaultValue", "Default"),
                                               &value,
                                               param.uiMin,
                                               param.uiMax) :
                            ImGui::DragFloat(
                                vultra::trId("materialGraph.blackboard.defaultValue", "Default"), &value, 0.01f);
                    if (!changed)
                        return false;
                    param.defaultValue = value;
                    return true;
                }
            }
        }

        constexpr uint64_t  kRenderTargetReleaseDelayFrames = 4u;
        constexpr glm::vec3 kPreviewWorldUp {0.0f, 1.0f, 0.0f};

        struct Bounds
        {
            glm::vec3 min {std::numeric_limits<float>::max()};
            glm::vec3 max {std::numeric_limits<float>::lowest()};
            bool      valid {false};

            void include(const glm::vec3& p)
            {
                min   = valid ? glm::min(min, p) : p;
                max   = valid ? glm::max(max, p) : p;
                valid = true;
            }

            [[nodiscard]] glm::vec3 center() const { return (min + max) * 0.5f; }
            [[nodiscard]] float     radius() const { return valid ? glm::length((max - min) * 0.5f) : 0.0f; }
        };

        void applyMaterialGraphAutoLayout(vultra::material_graph::Graph& graph)
        {
            std::vector<GraphLayoutNode> nodes;
            nodes.reserve(graph.nodes.size());
            for (size_t i = 0; i < graph.nodes.size(); ++i)
            {
                const auto& node = graph.nodes[i];
                nodes.push_back(GraphLayoutNode {
                    .id          = node.id,
                    .order       = static_cast<int>(i),
                    .inputCount  = static_cast<int>(node.inputs.size()),
                    .outputCount = static_cast<int>(node.outputs.size()),
                    .heightLanes = std::max(
                        1.0f,
                        (56.0f +
                         static_cast<float>(node.inputs.size() + node.outputs.size() + node.params.size()) * 24.0f) /
                            120.0f),
                    .sink = vultra::material_graph::isSurfaceOutputType(node.typeId),
                });
            }

            std::vector<GraphLayoutEdge> edges;
            edges.reserve(graph.links.size());
            for (const auto& link : graph.links)
            {
                int fromOrder = 0;
                if (const auto* source = vultra::material_graph::findNode(graph, link.from.nodeId))
                {
                    const auto it = std::ranges::find_if(source->outputs,
                                                         [&](const auto& pin) { return pin.name == link.from.pin; });
                    if (it != source->outputs.end())
                        fromOrder = static_cast<int>(std::distance(source->outputs.begin(), it));
                }

                int toOrder = 0;
                if (const auto* target = vultra::material_graph::findNode(graph, link.to.nodeId))
                {
                    const auto it =
                        std::ranges::find_if(target->inputs, [&](const auto& pin) { return pin.name == link.to.pin; });
                    if (it != target->inputs.end())
                        toOrder = static_cast<int>(std::distance(target->inputs.begin(), it));
                }
                edges.push_back({
                    .from      = link.from.nodeId,
                    .to        = link.to.nodeId,
                    .fromOrder = fromOrder,
                    .toOrder   = toOrder,
                });
            }

            const auto layout = computeLayeredGraphLayout(nodes,
                                                          edges,
                                                          GraphLayoutConfig {
                                                              .origin           = {-760.0f, -120.0f},
                                                              .columnSpacing    = 260.0f,
                                                              .rowSpacing       = 120.0f,
                                                              .sinkExtraSpacing = 400.0f,
                                                          });

            for (auto& node : graph.nodes)
            {
                if (auto it = layout.find(node.id); it != layout.end())
                    node.editor["pos"] = {it->second.x, it->second.y};
            }
        }

        glm::vec3 mapPreviewArcballPoint(const ImVec2& mouse, const ImVec2& min, const ImVec2& max)
        {
            const float width    = std::max(1.0f, max.x - min.x);
            const float height   = std::max(1.0f, max.y - min.y);
            const float diameter = std::max(1.0f, std::min(width, height));
            const float x        = (2.0f * (mouse.x - (min.x + width * 0.5f))) / diameter;
            const float y        = (-2.0f * (mouse.y - (min.y + height * 0.5f))) / diameter;
            const float len2     = x * x + y * y;

            if (len2 <= 1.0f)
                return glm::normalize(glm::vec3 {x, y, std::sqrt(std::max(0.0f, 1.0f - len2))});

            const float invLen = 1.0f / std::sqrt(len2);
            return glm::vec3 {x * invLen, y * invLen, 0.0f};
        }

        glm::quat arcballDelta(const glm::vec3& from, const glm::vec3& to)
        {
            const glm::vec3 axis     = glm::cross(from, to);
            const float     axisLen2 = glm::dot(axis, axis);
            if (axisLen2 <= 1e-8f)
                return glm::quat {1.0f, 0.0f, 0.0f, 0.0f};

            const float dot = std::clamp(glm::dot(from, to), -1.0f, 1.0f);
            return glm::normalize(glm::angleAxis(std::acos(dot), axis * glm::inversesqrt(axisLen2)));
        }

        std::filesystem::path assetRoot(const EditorContext& ctx)
        {
            return (ctx.state.currentProject / ctx.state.currentAssetRoot).lexically_normal();
        }

        std::filesystem::path pathForUri(const EditorContext& ctx, std::string_view uri)
        {
            constexpr std::string_view prefix = "res://";
            if (!uri.starts_with(prefix))
                return {};
            return (assetRoot(ctx) / std::filesystem::path(std::string(uri.substr(prefix.size())))).lexically_normal();
        }

        uint64_t fileWriteStamp(const std::filesystem::path& path)
        {
            std::error_code ec;
            const auto      time = std::filesystem::last_write_time(path, ec);
            if (ec)
                return 0;
            return static_cast<uint64_t>(time.time_since_epoch().count());
        }

        std::string uriForPath(const EditorContext& ctx, const std::filesystem::path& path)
        {
            std::error_code ec;
            const auto      rel = std::filesystem::relative(path.lexically_normal(), assetRoot(ctx), ec);
            if (ec || rel.empty())
                return {};
            return "res://" + rel.generic_string();
        }

        std::vector<std::string> collectGraphs(const EditorContext& ctx)
        {
            std::vector<std::string> out;
            const auto               root = assetRoot(ctx);
            std::error_code          ec;
            if (root.empty() || !std::filesystem::exists(root, ec))
                return out;

            for (auto it = std::filesystem::recursive_directory_iterator(
                     root, std::filesystem::directory_options::skip_permission_denied, ec);
                 it != std::filesystem::recursive_directory_iterator {};
                 it.increment(ec))
            {
                if (ec)
                {
                    ec.clear();
                    continue;
                }
                const auto& entry = *it;
                if (!entry.is_regular_file(ec))
                {
                    ec.clear();
                    continue;
                }
                const auto path = entry.path();
                const auto name = path.filename().generic_string();
                const auto ext  = path.extension().generic_string();
                if (ext != ".vmatgraph" && name.find(".vmatgraph.json") == std::string::npos)
                    continue;
                if (auto uri = uriForPath(ctx, path); !uri.empty())
                    out.push_back(std::move(uri));
            }
            std::ranges::sort(out);
            return out;
        }

        vultra::material_graph::Node
        makeNode(const vultra::material_graph::NodeDescriptor& desc, std::string id, ImVec2 pos)
        {
            vultra::material_graph::Node node;
            node.typeId      = desc.typeId;
            node.id          = std::move(id);
            node.displayName = desc.displayName;
            node.inputs      = desc.inputs;
            node.outputs     = desc.outputs;
            node.params      = desc.defaultParams;
            node.editor      = {{"pos", {pos.x, pos.y}}};
            return node;
        }

        float jsonFloat(const nlohmann::json& json, float fallback)
        {
            return json.is_number() ? json.get<float>() : fallback;
        }

        void setJsonVec(nlohmann::json& json, const float* values, int count)
        {
            json = nlohmann::json::array();
            for (int i = 0; i < count; ++i)
                json.push_back(values[i]);
        }

        void drawControlLabel(const char* label)
        {
            constexpr float kPropertyLabelWidth = 92.0f;
            const float     startX              = ImGui::GetCursorPosX();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label);
            ImGui::SameLine();
            ImGui::SetCursorPosX(startX + vultra::ui::dp(kPropertyLabelWidth));
        }

        bool drawJsonValue(EditorContext&                    ctx,
                           ui::TextureSelectorState&         textureSelector,
                           vultra::material_graph::ValueType type,
                           nlohmann::json&                   value,
                           const char*                       label)
        {
            switch (type)
            {
                case vultra::material_graph::ValueType::eBool: {
                    bool v = value.is_boolean() ? value.get<bool>() : false;
                    drawControlLabel(label);
                    if (ImGui::Checkbox("##value", &v))
                    {
                        value = v;
                        return true;
                    }
                    return false;
                }
                case vultra::material_graph::ValueType::eInt: {
                    int v = value.is_number_integer() ? value.get<int>() : 0;
                    drawControlLabel(label);
                    ImGui::SetNextItemWidth(vultra::ui::dp(92.0f));
                    if (ImGui::InputInt("##value", &v))
                    {
                        value = v;
                        return true;
                    }
                    return false;
                }
                case vultra::material_graph::ValueType::eFloat: {
                    float v = jsonFloat(value, 0.0f);
                    drawControlLabel(label);
                    ImGui::SetNextItemWidth(vultra::ui::dp(104.0f));
                    if (ImGui::DragFloat("##value", &v, 0.01f))
                    {
                        value = v;
                        return true;
                    }
                    return false;
                }
                case vultra::material_graph::ValueType::eVec2: {
                    float v[2] {0.0f, 0.0f};
                    if (value.is_array())
                        for (int i = 0; i < 2 && i < static_cast<int>(value.size()); ++i)
                            v[i] = jsonFloat(value[i], 0.0f);
                    drawControlLabel(label);
                    ImGui::SetNextItemWidth(vultra::ui::dp(136.0f));
                    if (ImGui::DragFloat2("##value", v, 0.01f))
                    {
                        setJsonVec(value, v, 2);
                        return true;
                    }
                    return false;
                }
                case vultra::material_graph::ValueType::eVec3: {
                    float v[3] {0.0f, 0.0f, 0.0f};
                    if (value.is_array())
                        for (int i = 0; i < 3 && i < static_cast<int>(value.size()); ++i)
                            v[i] = jsonFloat(value[i], 0.0f);
                    drawControlLabel(label);
                    ImGui::SetNextItemWidth(vultra::ui::dp(164.0f));
                    if (ImGui::DragFloat3("##value", v, 0.01f))
                    {
                        setJsonVec(value, v, 3);
                        return true;
                    }
                    return false;
                }
                case vultra::material_graph::ValueType::eColor:
                case vultra::material_graph::ValueType::eVec4: {
                    float v[4] {1.0f, 1.0f, 1.0f, 1.0f};
                    if (value.is_array())
                        for (int i = 0; i < 4 && i < static_cast<int>(value.size()); ++i)
                            v[i] = jsonFloat(value[i], 1.0f);
                    drawControlLabel(label);
                    ImGui::SetNextItemWidth(type == vultra::material_graph::ValueType::eColor ? vultra::ui::dp(92.0f) :
                                                                                                vultra::ui::dp(188.0f));
                    const bool changed =
                        type == vultra::material_graph::ValueType::eColor ?
                            ImGui::ColorEdit4(
                                "##value", v, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar) :
                            ImGui::DragFloat4("##value", v, 0.01f);
                    if (changed)
                    {
                        setJsonVec(value, v, 4);
                        return true;
                    }
                    return false;
                }
                case vultra::material_graph::ValueType::eTexture2D: {
                    std::string uri = value.is_string() ? value.get<std::string>() : std::string {};
                    drawControlLabel(label);
                    if (ui::drawTextureUriSelector(ctx,
                                                   "TextureSelectorPopup",
                                                   uri,
                                                   textureSelector,
                                                   ImVec2(vultra::ui::dp(184.0f), vultra::ui::dp(40.0f))))
                    {
                        value = uri;
                        return true;
                    }
                    return false;
                }
                case vultra::material_graph::ValueType::eString: {
                    std::array<char, 256> buffer {};
                    if (value.is_string())
                        std::snprintf(buffer.data(), buffer.size(), "%s", value.get<std::string>().c_str());
                    drawControlLabel(label);
                    ImGui::SetNextItemWidth(vultra::ui::dp(180.0f));
                    if (ImGui::InputText("##value", buffer.data(), buffer.size()))
                    {
                        value = std::string(buffer.data());
                        return true;
                    }
                    return false;
                }
                case vultra::material_graph::ValueType::eUnknown:
                default:
                    return false;
            }
        }

        bool drawStringCombo(nlohmann::json& value, const char* label, std::span<const char* const> options)
        {
            std::string current =
                value.is_string() ? value.get<std::string>() : std::string(options.empty() ? "" : options.front());
            bool changed = false;
            drawControlLabel(label);
            ImGui::SetNextItemWidth(vultra::ui::dp(156.0f));
            if (ImGui::BeginCombo("##value", current.c_str()))
            {
                for (const char* option : options)
                {
                    const bool selected = current == option;
                    if (ImGui::Selectable(option, selected))
                    {
                        current = option;
                        value   = current;
                        changed = true;
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            return changed;
        }

        bool drawKnownEnumParam(const vultra::material_graph::Node& node, const std::string& key, nlohmann::json& value)
        {
            if (!vultra::material_graph::isSurfaceOutputType(node.typeId))
                return false;

            if (key == "alphaMode")
            {
                static constexpr std::array options {
                    "Opaque",
                    "Mask",
                    "Blend",
                };
                return drawStringCombo(value, key.c_str(), options);
            }

            return false;
        }

        vultra::material_graph::ValueType parameterTypeForKey(const vultra::material_graph::NodeDescriptor& desc,
                                                              const std::string&                            key)
        {
            auto inputIt = std::ranges::find_if(desc.inputs, [&](const auto& pin) { return pin.name == key; });
            if (inputIt != desc.inputs.end())
                return inputIt->type;

            auto outputIt = std::ranges::find_if(desc.outputs, [&](const auto& pin) { return pin.name == key; });
            if (outputIt != desc.outputs.end())
                return outputIt->type;

            return vultra::material_graph::ValueType::eUnknown;
        }

        std::string nodeIdStem(const vultra::material_graph::NodeDescriptor& desc)
        {
            std::string id = desc.displayName;
            std::erase_if(id, [](char c) { return !std::isalnum(static_cast<unsigned char>(c)); });
            return id.empty() ? "Node" : id;
        }

        std::string_view nodeMenuCategory(std::string_view typeId)
        {
            if (typeId.starts_with("vultra.input.") || typeId == "vultra.param.texture2d")
                return "Inputs";
            if (typeId == "vultra.param.bool" || typeId == "vultra.param.int" || typeId == "vultra.param.enum")
                return "Logic";
            if (typeId.starts_with("vultra.param."))
                return "Parameters";
            if (typeId.starts_with("vultra.math."))
                return "Math";
            if (typeId.starts_with("vultra.texture."))
                return "Texture";
            if (typeId.starts_with("vultra.utility."))
                return "Shading";
            if (typeId.starts_with("vultra.output."))
                return "Output";
            return "Other";
        }

        // Per-category node title-bar colors so the graph reads at a glance: inputs
        // (Time/normals) and constant parameters get their own hues, the single surface
        // output is a distinct warm color, and operators are grouped by kind.
        struct NodeTitleColors
        {
            ImU32 bar;
            ImU32 hovered;
            ImU32 selected;
        };

        NodeTitleColors nodeTitleColors(std::string_view category)
        {
            const auto make = [](int r, int g, int b) {
                const auto lift = [](int c, int d) { return c + d > 255 ? 255 : c + d; };
                return NodeTitleColors {
                    IM_COL32(r, g, b, 255),
                    IM_COL32(lift(r, 35), lift(g, 35), lift(b, 35), 255),
                    IM_COL32(lift(r, 60), lift(g, 60), lift(b, 60), 255),
                };
            };
            if (category == "Inputs")
                return make(20, 110, 120); // teal  - engine inputs / constants (Time, normals)
            if (category == "Parameters")
                return make(40, 120, 70); // green - authored constant values (Float/Color)
            if (category == "Math")
                return make(45, 85, 150); // blue  - math operators
            if (category == "Logic")
                return make(95, 70, 150); // purple - bool/int/enum logic
            if (category == "Texture")
                return make(170, 95, 35); // orange - texture sampling
            if (category == "Shading")
                return make(150, 60, 110); // magenta - shading utilities (normal map, fresnel)
            if (category == "Output")
                return make(165, 55, 55); // red   - the single surface output (special)
            return make(80, 80, 88);      // gray  - uncategorized
        }

        // Outlined, slightly larger node title text - matches the Render Graph editor's
        // drawNodeTitleText so the two graph canvases read with a consistent node style.
        void drawNodeTitleText(const char* text, const float fontSize = vultra::ui::dp(18.0f))
        {
            const ImVec2 pos      = ImGui::GetCursorScreenPos();
            ImDrawList*  drawList = ImGui::GetWindowDrawList();
            ImFont*      font     = ImGui::GetFont();
            const ImU32  outline  = IM_COL32(0, 0, 0, 220);
            const ImU32  main     = ImGui::GetColorU32(ImGuiCol_Text);

            drawList->AddText(font, fontSize, ImVec2(pos.x - vultra::ui::dp(1.0f), pos.y), outline, text);
            drawList->AddText(font, fontSize, ImVec2(pos.x + vultra::ui::dp(1.0f), pos.y), outline, text);
            drawList->AddText(font, fontSize, ImVec2(pos.x, pos.y - vultra::ui::dp(1.0f)), outline, text);
            drawList->AddText(font, fontSize, ImVec2(pos.x, pos.y + vultra::ui::dp(1.0f)), outline, text);
            drawList->AddText(font, fontSize, pos, main, text);

            const float  scale    = fontSize / ImGui::GetFontSize();
            const ImVec2 textSize = ImGui::CalcTextSize(text);
            ImGui::Dummy(ImVec2 {textSize.x * scale, textSize.y * scale});
        }

        std::string_view nodeMenuSubcategory(std::string_view typeId)
        {
            if (typeId == "vultra.input.view_index" || typeId == "vultra.input.eye_index" ||
                typeId == "vultra.input.view_count" || typeId == "vultra.input.is_stereo_view")
                return "View";
            if (typeId.starts_with("vultra.input."))
                return "Vertex Attributes";
            if (typeId == "vultra.param.texture2d")
                return "Textures";
            return {};
        }

        // Localized (plain, no "###id") category/subcategory text. Node search matches BOTH the
        // English identifier (which users often remember) AND the label shown in the active language,
        // so a query works whether typed as "Math" or as the translated category.
        std::string_view localizedCategoryText(std::string_view english)
        {
            if (english == "Inputs")
                return vultra::tr("materialGraph.category.inputs");
            if (english == "Parameters")
                return vultra::tr("materialGraph.category.parameters");
            if (english == "Math")
                return vultra::tr("materialGraph.category.math");
            if (english == "Logic")
                return vultra::tr("materialGraph.category.logic");
            if (english == "Texture")
                return vultra::tr("materialGraph.category.texture");
            if (english == "Shading")
                return vultra::tr("materialGraph.category.shading");
            if (english == "Output")
                return vultra::tr("materialGraph.category.output");
            return vultra::tr("materialGraph.category.other");
        }

        std::string_view localizedSubcategoryText(std::string_view english)
        {
            if (english == "Vertex Attributes")
                return vultra::tr("materialGraph.subcategory.vertexAttributes");
            if (english == "Textures")
                return vultra::tr("materialGraph.subcategory.textures");
            return english;
        }

        std::string lowerAscii(std::string_view text)
        {
            std::string out(text);
            std::ranges::transform(
                out, out.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return out;
        }

        bool nodeMatchesSearch(const vultra::material_graph::NodeDescriptor& desc, std::string_view query)
        {
            if (query.empty())
                return true;

            const auto lowerQuery = lowerAscii(query);
            const auto matches    = [&](std::string_view value) {
                return !value.empty() && lowerAscii(value).find(lowerQuery) != std::string::npos;
            };
            const auto category    = nodeMenuCategory(desc.typeId);
            const auto subcategory = nodeMenuSubcategory(desc.typeId);
            return matches(desc.displayName) || matches(desc.typeId) || matches(category) ||
                   matches(localizedCategoryText(category)) || matches(subcategory) ||
                   matches(localizedSubcategoryText(subcategory));
        }

        bool hasInputLink(const vultra::material_graph::Graph& graph, std::string_view node, std::string_view pin)
        {
            return vultra::material_graph::findInputLink(graph, node, pin) != nullptr;
        }

        int stableImNodesId(std::string_view value)
        {
            uint32_t hash = 2166136261u;
            for (const unsigned char c : value)
            {
                hash ^= c;
                hash *= 16777619u;
            }
            hash &= 0x7fffffffu;
            if (hash == 0u || hash == static_cast<uint32_t>(std::numeric_limits<int>::min()))
                hash = 1u;
            return static_cast<int>(hash);
        }

        Bounds computeMeshBounds(EditorContext& ctx, const vultra::CoreUUID& meshUuid)
        {
            Bounds bounds;
            if (!meshUuid.valid())
            {
                bounds.include(glm::vec3 {-1.0f});
                bounds.include(glm::vec3 {1.0f});
                return bounds;
            }

            auto* assets = ctx.services ? ctx.services->tryGet<vultra::IAssetService>() : nullptr;
            if (!assets)
                return bounds;

            const auto mesh = assets->loadMeshAsync(meshUuid);
            if (!mesh.cpu())
                return bounds;

            if (mesh.cpu()->hasLocalBounds)
            {
                bounds.include(mesh.cpu()->localBoundsMin);
                bounds.include(mesh.cpu()->localBoundsMax);
                return bounds;
            }

            for (const auto& p : mesh.cpu()->positions)
                bounds.include(p);
            return bounds;
        }

        vultra::RenderCamera makePreviewCamera(const glm::vec3&      position,
                                               const glm::vec3&      targetPosition,
                                               const float           fovY,
                                               const float           aspect,
                                               vultra::rhi::Texture* target,
                                               const bool            useSkybox)
        {
            vultra::RenderCamera cam {};
            cam.name        = "Material Graph Preview";
            cam.view        = glm::lookAt(position, targetPosition, kPreviewWorldUp);
            cam.projection  = glm::perspectiveRH_ZO(glm::radians(fovY), std::max(aspect, 0.0001f), 0.05f, 1000.0f);
            cam.zNear       = 0.05f;
            cam.zFar        = 1000.0f;
            cam.fovY        = glm::radians(fovY);
            cam.target      = target;
            cam.clearValue  = {0.035f, 0.04f, 0.047f, 1.0f};
            cam.clearMode   = useSkybox ? 1u : 0u;
            cam.renderImGui = false;
            cam.rendererKey = "universal";
            return cam;
        }
    } // namespace

    MaterialGraphWindow::MaterialGraphWindow() :
        EditorWindow("Material Graph", ICON_MDI_MOLECULE, "window.materialGraph"),
        m_Registry(vultra::material_graph::makeBuiltinNodeRegistry())
    {
        m_NodeEditor = ImNodes::EditorContextCreate();
        m_PreviewWorld.setDebugName("Material Preview");
    }

    MaterialGraphWindow::~MaterialGraphWindow()
    {
        if (m_NodeEditor)
            ImNodes::EditorContextFree(m_NodeEditor);
    }

    void MaterialGraphWindow::draw(EditorContext& ctx)
    {
        consumeOpenRequest(ctx);
        refreshNodeRegistry(ctx);
        ensureLoaded(ctx);

        ImGuiWindowFlags flags = ImGuiWindowFlags_None;
        if (m_Dirty)
            flags |= ImGuiWindowFlags_UnsavedDocument;
        if (!ImGui::Begin(title().c_str(), &m_Open, flags))
        {
            ImGui::End();
            return;
        }
        const bool windowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        // Become the active document (owner) for undo/redo while focused: the global Ctrl+Z,
        // the History window, and editor.undo/redo then all act on this graph's history.
        // Ownership is sticky, so clicking the History panel does not hand it back to scene.
        claimActiveDocument(ctx, &m_History, windowFocused);

        drawToolbar(ctx);
        ImGui::Separator();

        const float rightWidth =
            std::clamp(ImGui::GetContentRegionAvail().x * 0.30f, vultra::ui::dp(320.0f), vultra::ui::dp(460.0f));
        const float leftWidth = std::max(
            vultra::ui::dp(240.0f), ImGui::GetContentRegionAvail().x - rightWidth - ImGui::GetStyle().ItemSpacing.x);
        ImGui::BeginChild("##MaterialGraphCanvas",
                          ImVec2(leftWidth, 0.0f),
                          true,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        drawNodeEditor(ctx);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##MaterialGraphSide", ImVec2(0.0f, 0.0f), true);
        drawPreview(ctx);
        ImGui::Separator();
        drawInspector(ctx);
        ImGui::EndChild();

        if (!ImGui::GetIO().WantTextInput && windowFocused && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S))
            saveGraph(ctx);

        // Undo/redo are handled globally on ctx.history (claimed above while focused), so
        // no per-window key handling is needed. Just capture a coalesced snapshot once
        // this frame's edits have settled.
        recordHistory();

        ImGui::End();
    }

    void MaterialGraphWindow::onDestroy(EditorContext& ctx)
    {
        m_TextureSelector.previewCache.clear(ctx);
        m_MeshSelector.previewCache.clear(ctx);
        if (ctx.services)
        {
            if (auto* cameras = ctx.services->tryGet<vultra::ICameraService>())
                cameras->removeManualCamerasByName("Material Graph Preview");
            if (auto* renderService = ctx.services->tryGet<vultra::IRenderService>())
                renderService->releaseOverrideRenderWorld(&m_PreviewWorld);
        }
        releasePreviewRenderTarget(ctx);
    }

    void MaterialGraphWindow::refreshNodeRegistry(EditorContext& ctx)
    {
        if (m_NodeRegistryAssetGeneration == ctx.state.assetFileGeneration)
            return;

        m_NodeRegistryAssetGeneration = ctx.state.assetFileGeneration;
        m_Registry                    = vultra::material_graph::makeBuiltinNodeRegistry();

        const auto      assetRoot = (ctx.state.currentProject / ctx.state.currentAssetRoot).lexically_normal();
        std::error_code ec;
        if (assetRoot.empty() || !std::filesystem::is_directory(assetRoot, ec))
            return;

        int loaded = 0;
        int failed = 0;
        for (auto it = std::filesystem::recursive_directory_iterator(assetRoot, ec);
             !ec && it != std::filesystem::recursive_directory_iterator();
             it.increment(ec))
        {
            if (ec)
                break;
            if (!it->is_regular_file(ec))
            {
                ec.clear();
                continue;
            }

            const auto path = it->path();
            const auto name = path.filename().generic_string();
            if (!name.ends_with(".vmatnode.json"))
                continue;

            std::ifstream file(path, std::ios::binary);
            if (!file)
            {
                ++failed;
                continue;
            }
            std::ostringstream text;
            text << file.rdbuf();
            auto parsed = vultra::material_graph::loadNodeDescriptorFromText(text.str());
            if (!parsed.ok() || !m_Registry.registerNode(std::move(parsed.descriptor)))
            {
                ++failed;
                continue;
            }
            ++loaded;
        }

        for (auto& node : m_Graph.nodes)
            ensureNodePorts(node);

        if (failed > 0)
            m_Status = vultra::trf("materialGraph.status.loadedCustomNodesSkipped", loaded, failed);
        else if (loaded > 0)
            m_Status = vultra::trf("materialGraph.status.loadedCustomNodes", loaded);
    }

    void MaterialGraphWindow::ensureLoaded(EditorContext& ctx)
    {
        if (m_Loaded)
        {
            const auto currentStamp = fileWriteStamp(pathForUri(ctx, m_CurrentUri));
            if (m_LoadedAssetGeneration != ctx.state.assetFileGeneration && currentStamp != m_LoadedWriteStamp)
            {
                if (loadGraph(ctx, m_CurrentUri))
                    m_Status = vultra::tr("materialGraph.status.reloaded");
            }
            return;
        }
        m_Loaded = true;
        if (!loadGraph(ctx, m_CurrentUri))
            newGraph(ctx);
    }

    void MaterialGraphWindow::consumeOpenRequest(EditorContext& ctx)
    {
        if (!ctx.state.materialGraphOpenRequested)
            return;

        const auto uri                       = ctx.state.currentEditingMaterialGraph;
        ctx.state.materialGraphOpenRequested = false;
        if (uri.empty())
            return;

        if (uri == m_CurrentUri && m_Loaded)
        {
            m_Status                = vultra::tr("materialGraph.status.alreadyOpen");
            ctx.state.statusMessage = vultra::trf("materialGraph.status.alreadyOpenMessage", uri);
            return;
        }

        m_Loaded = true;
        if (loadGraph(ctx, uri))
        {
            ctx.state.statusMessage = vultra::trf("materialGraph.status.openedMessage", uri);
        }
        else
        {
            ctx.state.statusMessage = vultra::trf("materialGraph.status.openFailedMessage", uri);
            m_Status                = vultra::tr("materialGraph.status.loadFailed");
        }
    }

    void MaterialGraphWindow::newGraph(EditorContext& ctx)
    {
        m_Graph            = {};
        m_Graph.name       = "Default Material Graph";
        m_CurrentUri       = "res://materials/default.vmatgraph.json";
        const auto* color  = m_Registry.find("vultra.param.color");
        const auto* output = m_Registry.find("vultra.output.pbr_mr");
        if (color && output)
        {
            m_Graph.nodes.push_back(makeNode(*color, "Color", ImVec2(-220.0f, 20.0f)));
            m_Graph.nodes.push_back(makeNode(*output, "Surface", ImVec2(160.0f, 0.0f)));
            m_Graph.links.push_back(
                {.from = {.nodeId = "Color", .pin = "value"}, .to = {.nodeId = "Surface", .pin = "baseColor"}});
        }
        markDirty(ctx);
        resetHistory();
    }

    bool MaterialGraphWindow::loadGraph(EditorContext& ctx, std::string uri)
    {
        auto* assets = ctx.services ? ctx.services->tryGet<vultra::IAssetService>() : nullptr;
        if (!assets)
            return false;
        auto text = assets->loadTextAssetSync(uri);
        if (!text)
            return false;
        std::vector<vultra::material_graph::Diagnostic> diagnostics;
        auto graph    = vultra::material_graph::loadGraphFromText(text.value(), &diagnostics);
        m_Diagnostics = std::move(diagnostics);
        if (!graph)
            return false;
        m_Graph = std::move(*graph);
        for (auto& node : m_Graph.nodes)
            ensureNodePorts(node);
        m_CurrentUri            = std::move(uri);
        m_Dirty                 = false;
        m_Status                = vultra::tr("materialGraph.status.loaded");
        m_LoadedAssetGeneration = ctx.state.assetFileGeneration;
        m_LoadedWriteStamp      = fileWriteStamp(pathForUri(ctx, m_CurrentUri));
        resetHistory();
        return true;
    }

    bool MaterialGraphWindow::saveGraph(EditorContext& ctx)
    {
        const auto path = pathForUri(ctx, m_CurrentUri);
        if (path.empty())
            return false;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file)
        {
            m_Status = vultra::tr("materialGraph.status.saveFailed");
            return false;
        }
        file << vultra::material_graph::saveGraphToText(m_Graph);
        file.close();
        if (auto* assets = ctx.services ? ctx.services->tryGet<vultra::IAssetService>() : nullptr)
        {
            assets->clearTextAssetOverride(m_CurrentUri);
            assets->reimportAsset(m_CurrentUri, true);
        }
        m_Dirty                 = false;
        m_Status                = vultra::tr("materialGraph.status.saved");
        m_LoadedAssetGeneration = ctx.state.assetFileGeneration;
        m_LoadedWriteStamp      = fileWriteStamp(path);
        (void)saveThumbnail(ctx, path);
        if (m_LiveApply)
            compileGraph(ctx);
        return true;
    }

    bool MaterialGraphWindow::saveThumbnail(EditorContext& ctx, const std::filesystem::path& sourcePath)
    {
        if (!ctx.thumbnails || !ctx.services || !m_PreviewTarget.texture)
            return false;

        auto* backend = ctx.services->tryGet<vultra::IRenderBackendService>();
        if (!backend)
            return false;

        const auto request = ctx.thumbnails->requestMaterialGraph(ctx, sourcePath);
        if (request.outputPath.empty())
            return false;

        std::error_code ec;
        std::filesystem::create_directories(request.outputPath.parent_path(), ec);
        if (ec)
            return false;

        const bool saved = backend->renderDevice().saveTextureToFile(
            *m_PreviewTarget.texture, request.outputPath.generic_string(), vultra::rhi::ImageAspect::eColor);
        if (saved)
            ctx.thumbnails->markReady(request);
        return saved;
    }

    bool MaterialGraphWindow::compileGraph(EditorContext& ctx)
    {
        vultra::material_graph::MaterialGraphCompiler  compiler {m_Registry};
        vultra::material_graph::SurfaceFunctionBackend backend;
        const auto                                     shaderId =
            vultra::material_graph::sanitizeShaderId(std::filesystem::path(m_CurrentUri).stem().generic_string());
        auto result = compiler.compile(
            {.graph = m_Graph, .shaderId = shaderId, .graphId = vultra::material_graph::stableGraphId(m_CurrentUri)},
            backend);
        if (!result)
        {
            m_Diagnostics = result.error();
            m_Status      = vultra::tr("materialGraph.status.compileFailed");
            return false;
        }
        m_Diagnostics = result->diagnostics;

        const auto outDir = ctx.state.currentProject / ".vultra" / "generated" / "shaders" / "material_graph";
        std::filesystem::create_directories(outDir);
        const auto    outPath = outDir / (shaderId + ".frag.vshader");
        std::ofstream file(outPath, std::ios::binary | std::ios::trunc);
        if (!file)
        {
            m_Status = vultra::tr("materialGraph.status.writeShaderFailed");
            return false;
        }
        file << "[vshader]\nid = \"project/material_graph/" << shaderId
             << ".frag\"\nlanguage = glsl\nversion = 460\n\n[frag]\n";
        file << "#extension GL_EXT_nonuniform_qualifier : require\n\n";
        file << "#define VULTRA_DECLARE_BINDLESS_TEXTURES\n";
        file << "#include \"include/common/gpu_scene.glsl\"\n\n";
        file << result->vshaderSource << "\n";
        file << "layout(location = 0) out vec4 FragColor;\n";
        file << "void main()\n{\n";
        file << "    MaterialGraphSurface surface = eval_material_graph_"
             << vultra::material_graph::sanitizeShaderId(shaderId)
             << "(0u, vec2(0.0), vec3(0.0), vec3(0.0, 1.0, 0.0), vec3(0.0, 0.0, 1.0), 0.0);\n";
        file << "    FragColor = surface.baseColor;\n";
        file << "}\n";
        file.close();

        // Also emit a mesh-material fragment (a real GBuffer-writing material
        // shader) next to the standalone preview, so the graph can render through
        // the eShaderMaterial path via its per-pixel GLSL. The reimport below cooks
        // it for both Vulkan (.vshlib) and WebGPU (.vshweblib).
        {
            vultra::material_graph::MeshMaterialBackend meshBackend;
            auto meshResult = compiler.compile({.graph    = m_Graph,
                                                .shaderId = shaderId,
                                                .graphId  = vultra::material_graph::stableGraphId(m_CurrentUri)},
                                               meshBackend);
            if (meshResult)
            {
                const auto    meshPath = outDir / (shaderId + ".material.frag.vshader");
                std::ofstream meshFile(meshPath, std::ios::binary | std::ios::trunc);
                if (meshFile)
                    meshFile << meshResult->vshaderSource;
            }
        }

        if (auto* assets = ctx.services ? ctx.services->tryGet<vultra::IAssetService>() : nullptr)
            assets->reimportAsset("res://shaders/project.vshaderlib.lua", true);
        if (auto* shaders = ctx.services ? ctx.services->tryGet<vultra::IShaderService>() : nullptr)
            (void)shaders->reloadProjectLibrary("res://shaders/project.vshaderlib.lua");
        m_Status = vultra::trf("materialGraph.status.compiled", outPath.generic_string());
        return true;
    }

    void MaterialGraphWindow::drawToolbar(EditorContext& ctx)
    {
        ImGui::SetNextItemWidth(vultra::ui::dp(330.0f));
        if (ImGui::BeginCombo(vultra::trId("materialGraph.toolbar.graph", "Graph"), m_CurrentUri.c_str()))
        {
            for (const auto& uri : collectGraphs(ctx))
                if (ImGui::Selectable(uri.c_str(), uri == m_CurrentUri))
                    loadGraph(ctx, uri);
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button((std::string {ICON_MDI_FILE_PLUS " "} + vultra::tr("materialGraph.toolbar.new")).c_str()))
            newGraph(ctx);
        ImGui::SameLine();
        ImGui::BeginDisabled(!m_History.canUndo());
        if (ImGui::Button(ICON_MDI_UNDO "###materialGraphUndo"))
            undo(ctx);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", vultra::tr("history.undo"));
        ImGui::SameLine();
        ImGui::BeginDisabled(!m_History.canRedo());
        if (ImGui::Button(ICON_MDI_REDO "###materialGraphRedo"))
            redo(ctx);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", vultra::tr("history.redo"));
        ImGui::SameLine();
        if (ImGui::Button((std::string {ICON_MDI_CONTENT_SAVE " "} + vultra::tr("common.save")).c_str()))
            saveGraph(ctx);
        ImGui::SameLine();
        if (ImGui::Button((std::string {ICON_MDI_COG_PLAY " "} + vultra::tr("materialGraph.toolbar.compile")).c_str()))
            compileGraph(ctx);
        ImGui::SameLine();
        if (ImGui::Button((std::string {ICON_MDI_GRAPH " "} + vultra::tr("materialGraph.toolbar.autoLayout")).c_str()))
        {
            applyMaterialGraphAutoLayout(m_Graph);
            markDirty(ctx);
            m_Status = vultra::tr("materialGraph.status.autoLayoutApplied");
        }
        ImGui::SameLine();
        ImGui::Checkbox(vultra::tr("materialGraph.toolbar.liveApply"), &m_LiveApply);
        if (!m_Status.empty())
        {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", m_Status.c_str());
        }
    }

    void MaterialGraphWindow::drawNodeEditor(EditorContext& ctx)
    {
        m_Pins.clear();
        ImNodes::EditorContextSet(m_NodeEditor);
        ImNodes::BeginNodeEditor();
        for (auto& node : m_Graph.nodes)
        {
            ensureNodePorts(node);
            const int id = nodeId(node.id);

            const auto titleColors = nodeTitleColors(nodeMenuCategory(node.typeId));
            ImNodes::PushColorStyle(ImNodesCol_TitleBar, titleColors.bar);
            ImNodes::PushColorStyle(ImNodesCol_TitleBarHovered, titleColors.hovered);
            ImNodes::PushColorStyle(ImNodesCol_TitleBarSelected, titleColors.selected);

            ImNodes::BeginNode(id);
            ImNodes::BeginNodeTitleBar();
            {
                // Line 1: the node TYPE name (so a Multiply still reads as "Multiply").
                const auto*       titleDesc = m_Registry.find(node.typeId);
                const std::string typeName  = titleDesc && !titleDesc->displayName.empty() ?
                                                  titleDesc->displayName :
                                                  (!node.displayName.empty() ? node.displayName : node.typeId);
                drawNodeTitleText(typeName.c_str());
                // Line 2: the user note (falls back to a legacy displayName label if distinct).
                const std::string noteText =
                    !node.note.empty() ? node.note : (node.displayName != typeName ? node.displayName : std::string {});
                if (!noteText.empty())
                {
                    // The title bar is now category-colored, so a disabled-gray note is
                    // unreadable. Use a near-white that contrasts on every title hue while
                    // staying a touch dimmer than the type name to keep the hierarchy.
                    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(236, 238, 245, 230));
                    ImGui::TextUnformatted(noteText.c_str());
                    ImGui::PopStyleColor();
                }
            }
            ImNodes::EndNodeTitleBar();

            for (const auto& pin : node.inputs)
            {
                ImNodes::BeginInputAttribute(pinId(node.id, pin.name, true), ImNodesPinShape_CircleFilled);
                // When the pin has no incoming link and carries an editable default, draw
                // its value editor inline on the pin row instead of a separate param row
                // below (and instead of a bare name). A linked pin shows just its name.
                if (!hasInputLink(m_Graph, node.id, pin.name) && node.params.contains(pin.name))
                {
                    auto& value = node.params[pin.name];
                    ImGui::PushID(pin.name.c_str());
                    const bool changed = drawKnownEnumParam(node, pin.name, value) ||
                                         drawJsonValue(ctx, m_TextureSelector, pin.type, value, pin.name.c_str());
                    ImGui::PopID();
                    if (changed)
                        markDirty(ctx);
                }
                else
                {
                    ImGui::TextUnformatted(pin.name.c_str());
                }
                ImNodes::EndInputAttribute();
            }

            // Param-only rows: params with no matching input pin (e.g. alphaMode,
            // shadingModelName). Pin-backed params are edited inline on the pin row above.
            for (const auto& [key, raw] : node.params.items())
            {
                if (std::ranges::any_of(node.inputs, [&](const auto& pin) { return pin.name == key; }))
                    continue;
                auto*                             desc = m_Registry.find(node.typeId);
                vultra::material_graph::ValueType type = vultra::material_graph::ValueType::eUnknown;
                if (desc)
                    type = parameterTypeForKey(*desc, key);
                auto& value = node.params[key];
                ImGui::PushID(key.c_str());
                const bool changed = drawKnownEnumParam(node, key, value) ||
                                     drawJsonValue(ctx, m_TextureSelector, type, value, key.c_str());
                ImGui::PopID();
                if (changed)
                    markDirty(ctx);
            }

            // Output pins sit on the node's right edge, so right-align their labels
            // (inputs stay left-aligned next to their left-edge pins). The node width
            // comes from the previous frame's layout; nodes are static so it converges.
            const float nodeContentWidth = ImNodes::GetNodeDimensions(id).x - ImNodes::GetStyle().NodePadding.x * 2.0f;
            // A single-output node's pin is unambiguous, so its name label is redundant
            // (e.g. a Float/Color node already shows its value editor); draw just the pin.
            // Multi-output nodes keep right-aligned labels to tell the pins apart.
            const bool labelOutputs = node.outputs.size() > 1;
            for (const auto& pin : node.outputs)
            {
                ImNodes::BeginOutputAttribute(pinId(node.id, pin.name, false), ImNodesPinShape_CircleFilled);
                if (labelOutputs)
                {
                    const float labelWidth = ImGui::CalcTextSize(pin.name.c_str()).x;
                    if (nodeContentWidth > labelWidth)
                        ImGui::Indent(nodeContentWidth - labelWidth);
                    ImGui::TextUnformatted(pin.name.c_str());
                }
                else
                {
                    // Reserve a row so the pin marker keeps a sensible vertical position.
                    ImGui::Dummy(ImVec2(1.0f, ImGui::GetTextLineHeight()));
                }
                ImNodes::EndOutputAttribute();
            }
            ImNodes::EndNode();
            ImNodes::PopColorStyle(); // TitleBarSelected
            ImNodes::PopColorStyle(); // TitleBarHovered
            ImNodes::PopColorStyle(); // TitleBar

            if (node.editor.contains("pos"))
            {
                const auto& pos = node.editor["pos"];
                if (pos.is_array() && pos.size() == 2)
                    ImNodes::SetNodeGridSpacePos(id, ImVec2(pos[0].get<float>(), pos[1].get<float>()));
                node.editor.erase("pos");
            }
        }

        for (const auto& link : m_Graph.links)
            ImNodes::Link(
                linkId(link), pinId(link.from.nodeId, link.from.pin, false), pinId(link.to.nodeId, link.to.pin, true));
        ImNodes::MiniMap(0.18f, ImNodesMiniMapLocation_BottomRight);
        ImNodes::EndNodeEditor();

        for (auto& node : m_Graph.nodes)
        {
            const auto pos     = ImNodes::GetNodeGridSpacePos(nodeId(node.id));
            node.editor["pos"] = {pos.x, pos.y};
        }

        int start = 0;
        int end   = 0;
        if (ImNodes::IsLinkCreated(&start, &end))
        {
            auto a = m_Pins.find(start);
            auto b = m_Pins.find(end);
            if (a != m_Pins.end() && b != m_Pins.end())
            {
                auto from = a->second;
                auto to   = b->second;
                if (from.input)
                    std::swap(from, to);
                if (!from.input && to.input)
                {
                    std::erase_if(m_Graph.links,
                                  [&](const auto& link) { return link.to.nodeId == to.node && link.to.pin == to.pin; });
                    m_Graph.links.push_back(
                        {.from = {.nodeId = from.node, .pin = from.pin}, .to = {.nodeId = to.node, .pin = to.pin}});
                    markDirty(ctx);
                }
            }
        }

        int destroyed = 0;
        if (ImNodes::IsLinkDestroyed(&destroyed))
        {
            std::erase_if(m_Graph.links, [&](const auto& link) { return linkId(link) == destroyed; });
            markDirty(ctx);
        }

        const bool canvasFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        const bool canvasHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows |
                                                          ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

        if (canvasFocused && ImGui::IsKeyPressed(ImGuiKey_Delete))
        {
            int selectedCount = ImNodes::NumSelectedNodes();
            if (selectedCount > 0)
            {
                std::vector<int> selected(static_cast<size_t>(selectedCount));
                ImNodes::GetSelectedNodes(selected.data());
                for (const int selectedNode : selected)
                {
                    std::string node;
                    for (const auto& item : m_Pins)
                        if (nodeId(item.second.node) == selectedNode)
                            node = item.second.node;
                    if (!node.empty() && node != "Surface")
                    {
                        std::erase_if(m_Graph.links, [&](const auto& link) {
                            return link.from.nodeId == node || link.to.nodeId == node;
                        });
                        std::erase_if(m_Graph.nodes, [&](const auto& n) { return n.id == node; });
                        markDirty(ctx);
                    }
                }
            }
        }

        int        hovered     = 0;
        const bool nodeHovered = ImNodes::IsNodeHovered(&hovered);
        if (canvasHovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
        {
            if (nodeHovered)
            {
                m_ContextNode = hovered;
                ImGui::OpenPopup("MaterialGraphNodeMenu");
            }
            else
            {
                ImGui::OpenPopup("MaterialGraphAddNode");
            }
        }
        const auto contextNodeId = [&]() -> std::string {
            for (const auto& n : m_Graph.nodes)
                if (nodeId(n.id) == m_ContextNode)
                    return n.id;
            return {};
        };
        const auto findNode = [&](const std::string& id) -> vultra::material_graph::Node* {
            for (auto& n : m_Graph.nodes)
                if (n.id == id)
                    return &n;
            return nullptr;
        };

        if (ImGui::BeginPopup("MaterialGraphNodeMenu"))
        {
            const std::string node = contextNodeId();
            if (ImGui::MenuItem(
                    (std::string {ICON_MDI_NOTE_EDIT " "} + vultra::tr("materialGraph.nodeMenu.editNote")).c_str()))
            {
                m_NoteEditNode = node;
                m_NoteEditBuffer.fill('\0');
                if (auto* n = findNode(node))
                {
                    const auto& current = !n->note.empty() ? n->note : n->displayName;
                    std::snprintf(m_NoteEditBuffer.data(), m_NoteEditBuffer.size(), "%s", current.c_str());
                }
                m_OpenNoteEditor = true;
            }
            if (auto* n = findNode(node); n && (!n->note.empty() || !n->displayName.empty()))
            {
                if (ImGui::MenuItem(
                        (std::string {ICON_MDI_NOTE_OFF " "} + vultra::tr("materialGraph.nodeMenu.clearNote")).c_str()))
                {
                    n->note.clear();
                    n->displayName.clear();
                    markDirty(ctx);
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem((std::string {ICON_MDI_DELETE " "} + vultra::tr("common.delete")).c_str()))
            {
                if (!node.empty() && node != "Surface")
                {
                    std::erase_if(m_Graph.links,
                                  [&](const auto& link) { return link.from.nodeId == node || link.to.nodeId == node; });
                    std::erase_if(m_Graph.nodes, [&](const auto& n) { return n.id == node; });
                    markDirty(ctx);
                }
            }
            ImGui::EndPopup();
        }

        if (m_OpenNoteEditor)
        {
            ImGui::OpenPopup("MaterialGraphEditNote");
            m_OpenNoteEditor = false;
        }
        if (ImGui::BeginPopup("MaterialGraphEditNote"))
        {
            ImGui::TextUnformatted(vultra::tr("materialGraph.nodeMenu.note"));
            ImGui::SetNextItemWidth(vultra::ui::dp(240.0f));
            const bool entered = ImGui::InputText(
                "##note", m_NoteEditBuffer.data(), m_NoteEditBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue);
            const bool apply = ImGui::Button(vultra::tr("common.ok")) || entered;
            ImGui::SameLine();
            const bool cancel = ImGui::Button(vultra::tr("common.cancel"));
            if (apply)
            {
                if (auto* n = findNode(m_NoteEditNode))
                {
                    n->note = m_NoteEditBuffer.data();
                    n->displayName.clear(); // migrate legacy label into the note
                    markDirty(ctx);
                }
                ImGui::CloseCurrentPopup();
            }
            else if (cancel)
            {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        drawAddNodePopup(ctx);
    }

    void MaterialGraphWindow::drawInspector(EditorContext& ctx)
    {
        ImGui::TextUnformatted(vultra::tr("materialGraph.inspector.blackboard"));
        if (ImGui::SmallButton(
                (std::string {ICON_MDI_PLUS " "} + vultra::tr("materialGraph.inspector.addParameter")).c_str()))
        {
            std::string name   = "param";
            int         suffix = 1;
            const auto  exists = [&]() {
                return std::any_of(m_Graph.blackboard.begin(), m_Graph.blackboard.end(), [&](const auto& p) {
                    return p.name == name;
                });
            };
            while (exists())
                name = "param" + std::to_string(++suffix);

            m_Graph.blackboard.push_back(vultra::material_graph::BlackboardParameter {
                .name         = name,
                .type         = vultra::material_graph::ValueType::eFloat,
                .defaultValue = 0.0f,
                .displayName  = name,
                .uiMin        = 0.0f,
                .uiMax        = 1.0f,
                .hasUiRange   = true,
            });
            markDirty(ctx);
        }

        if (m_Graph.blackboard.empty())
        {
            ImGui::TextDisabled("%s", vultra::tr("materialGraph.inspector.noExposedParameters"));
        }
        else
        {
            int removeIndex = -1;
            for (int i = 0; i < static_cast<int>(m_Graph.blackboard.size()); ++i)
            {
                auto& param = m_Graph.blackboard[static_cast<size_t>(i)];
                ImGui::PushID(i);
                const auto header = param.displayName.empty() ? param.name : param.displayName;
                if (ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                {
                    if (inputStringField(vultra::trId("common.name", "Name"), param.name))
                        markDirty(ctx);
                    if (inputStringField(vultra::trId("materialGraph.inspector.displayName", "Display Name"),
                                         param.displayName))
                        markDirty(ctx);

                    int typeIndex = blackboardTypeIndex(param.type);
                    if (ImGui::Combo(vultra::trId("common.type", "Type"),
                                     &typeIndex,
                                     kBlackboardTypeLabels,
                                     IM_ARRAYSIZE(kBlackboardTypeLabels)))
                    {
                        param.type         = blackboardTypeFromIndex(typeIndex);
                        param.defaultValue = defaultBlackboardValue(param.type);
                        if (param.type != vultra::material_graph::ValueType::eFloat)
                            param.hasUiRange = false;
                        markDirty(ctx);
                    }

                    if (drawBlackboardDefaultValue(param))
                        markDirty(ctx);

                    const bool rangeSupported = param.type == vultra::material_graph::ValueType::eFloat;
                    ImGui::BeginDisabled(!rangeSupported);
                    if (ImGui::Checkbox(vultra::trId("materialGraph.inspector.uiRange", "UI Range"), &param.hasUiRange))
                        markDirty(ctx);
                    if (param.hasUiRange)
                    {
                        if (ImGui::DragFloat(vultra::trId("materialGraph.inspector.min", "Min"), &param.uiMin, 0.01f))
                            markDirty(ctx);
                        if (ImGui::DragFloat(vultra::trId("materialGraph.inspector.max", "Max"), &param.uiMax, 0.01f))
                            markDirty(ctx);
                        if (param.uiMax < param.uiMin)
                            param.uiMax = param.uiMin;
                    }
                    ImGui::EndDisabled();

                    if (ImGui::SmallButton(
                            (std::string {ICON_MDI_DELETE_OUTLINE " "} + vultra::tr("common.remove")).c_str()))
                        removeIndex = i;
                }
                ImGui::PopID();
            }
            if (removeIndex >= 0)
            {
                m_Graph.blackboard.erase(m_Graph.blackboard.begin() + removeIndex);
                markDirty(ctx);
            }
        }

        ImGui::Separator();
        ImGui::TextUnformatted(vultra::tr("materialGraph.inspector.diagnostics"));
        if (m_Diagnostics.empty())
            ImGui::TextDisabled("%s", vultra::tr("materialGraph.inspector.noDiagnostics"));
        for (const auto& diag : m_Diagnostics)
        {
            const ImVec4 color = diag.severity == vultra::material_graph::Diagnostic::Severity::eError ?
                                     ImVec4(1.0f, 0.35f, 0.35f, 1.0f) :
                                     ImVec4(1.0f, 0.75f, 0.25f, 1.0f);
            ImGui::TextColored(
                color, "%s%s%s", diag.message.c_str(), diag.nodeId.empty() ? "" : " @ ", diag.nodeId.c_str());
        }
    }

    void MaterialGraphWindow::updatePreviewFocusAnimation()
    {
        if (!m_PreviewFocusActive)
            return;

        const float dt          = std::max(ImGui::GetIO().DeltaTime, 0.0f);
        m_PreviewFocusElapsed   = std::min(m_PreviewFocusElapsed + dt, m_PreviewFocusDuration);
        const float t           = m_PreviewFocusDuration > 0.0f ?
                                      std::clamp(m_PreviewFocusElapsed / m_PreviewFocusDuration, 0.0f, 1.0f) :
                                      1.0f;
        const float eased       = 1.0f - std::pow(1.0f - t, 3.0f);
        m_PreviewCameraPosition = glm::mix(m_PreviewFocusStartPosition, m_PreviewFocusTargetPosition, eased);

        if (t >= 1.0f)
            m_PreviewFocusActive = false;
    }

    bool MaterialGraphWindow::focusPreviewMesh(EditorContext& ctx, const float aspect, const bool resetAngle)
    {
        const auto bounds = computeMeshBounds(ctx, m_PreviewMesh);
        if (!bounds.valid)
            return false;

        if (resetAngle)
        {
            m_PreviewObjectRotation = glm::quat {1.0f, 0.0f, 0.0f, 0.0f};
            m_PreviewArcballActive  = false;
            m_PreviewDistanceScale  = 1.0f;
        }

        m_PreviewBoundsCenter          = bounds.center();
        const float radius             = std::max(bounds.radius(), 0.25f);
        const float fovY               = glm::radians(std::clamp(m_PreviewCameraFovY, 5.0f, 160.0f));
        const float safeAspect         = std::max(aspect, 0.0001f);
        const float tanY               = std::tan(fovY * 0.5f);
        const float tanX               = tanY * safeAspect;
        const float fitDistance        = radius / std::max(std::min(tanX, tanY), 0.0001f);
        m_PreviewFitDistance           = std::max(fitDistance * 1.08f, radius + 0.35f);
        const glm::vec3 orbitDirection = glm::normalize(glm::vec3 {0.55f, 0.32f, 0.74f});
        const glm::vec3 targetPosition =
            m_PreviewBoundsCenter + orbitDirection * m_PreviewFitDistance * m_PreviewDistanceScale;

        m_PreviewFocusStartPosition  = resetAngle ? targetPosition : m_PreviewCameraPosition;
        m_PreviewFocusTargetPosition = targetPosition;
        m_PreviewFocusElapsed        = 0.0f;
        m_PreviewFocusDuration       = resetAngle ? 0.0f : 0.28f;
        m_PreviewFocusActive =
            !resetAngle && glm::length(m_PreviewFocusTargetPosition - m_PreviewFocusStartPosition) > 0.0001f;
        if (!m_PreviewFocusActive)
            m_PreviewCameraPosition = m_PreviewFocusTargetPosition;
        return true;
    }

    void MaterialGraphWindow::drawPreview(EditorContext& ctx)
    {
        ensurePreviewWorld(ctx);
        if (m_PreviewSphere != entt::null)
        {
            auto& reg = m_PreviewWorld.registry();
            if (auto* mesh = m_PreviewWorld.registry().try_get<vultra::MeshComponent>(m_PreviewSphere))
            {
                mesh->mesh            = m_PreviewMesh;
                mesh->builtinGeometry = m_PreviewMesh.valid() ? UINT32_MAX : 2u;
                if (mesh->materialOverrides.empty())
                    mesh->materialOverrides.push_back({});
                mesh->materialOverrides.front().slot          = 0u;
                mesh->materialOverrides.front().materialGraph = m_CurrentUri;
            }
            if (auto* tr = reg.try_get<vultra::TransformComponent>(m_PreviewSphere))
            {
                tr->rotation    = m_PreviewObjectRotation;
                tr->position    = glm::vec3 {0.0f};
                tr->scale       = glm::vec3 {1.0f};
                tr->worldMatrix = glm::translate(glm::mat4 {1.0f}, m_PreviewBoundsCenter) *
                                  glm::mat4_cast(tr->rotation) *
                                  glm::translate(glm::mat4 {1.0f}, -m_PreviewBoundsCenter);
                tr->dirty       = false;
            }
            if (m_PreviewEnvironment != entt::null && reg.valid(m_PreviewEnvironment))
            {
                if (auto* environment = reg.try_get<vultra::EnvironmentComponent>(m_PreviewEnvironment))
                {
                    environment->skybox           = m_PreviewSkybox;
                    environment->ambientColor     = glm::vec3 {0.28f, 0.30f, 0.34f};
                    environment->ambientIntensity = 1.6f;
                    environment->enableIBL        = m_PreviewSkybox.valid();
                    environment->iblColor         = glm::vec3 {0.45f, 0.48f, 0.52f};
                    environment->iblIntensity     = 1.2f;
                }
            }
        }
        const float size   = std::min(ImGui::GetContentRegionAvail().x, vultra::ui::dp(260.0f));
        const float aspect = 1.0f;
        if (m_LastPreviewMesh != m_PreviewMesh)
        {
            m_LastPreviewMesh = m_PreviewMesh;
            focusPreviewMesh(ctx, aspect, true);
        }
        updatePreviewFocusAnimation();
        if (!m_PreviewFocusActive)
        {
            const glm::vec3 orbitDirection = glm::normalize(glm::vec3 {0.55f, 0.32f, 0.74f});
            m_PreviewCameraPosition =
                m_PreviewBoundsCenter + orbitDirection * m_PreviewFitDistance * m_PreviewDistanceScale;
        }

        if (m_PreviewLight != entt::null && m_PreviewWorld.registry().valid(m_PreviewLight))
        {
            if (auto* lightTransform = m_PreviewWorld.registry().try_get<vultra::TransformComponent>(m_PreviewLight))
            {
                const glm::vec3 lightDirection = glm::normalize(m_PreviewBoundsCenter - m_PreviewCameraPosition);
                lightTransform->rotation       = glm::quatLookAt(lightDirection, kPreviewWorldUp);
                lightTransform->dirty          = true;
            }
        }

        ensurePreviewRenderTarget(
            ctx, static_cast<uint32_t>(std::max(size, 32.0f)), static_cast<uint32_t>(std::max(size, 32.0f)));
        const float previewDeltaTime = m_PreviewTimePlaying ? std::max(ImGui::GetIO().DeltaTime, 0.0f) : 0.0f;
        if (m_PreviewTimePlaying)
            m_PreviewTimeSeconds = std::max(0.0f, m_PreviewTimeSeconds + previewDeltaTime);
        if (ctx.services && m_PreviewTarget.texture)
        {
            if (auto* cameras = ctx.services->tryGet<vultra::ICameraService>())
            {
                cameras->removeManualCamerasByName("Material Graph Preview");
                auto cam              = makePreviewCamera(m_PreviewCameraPosition,
                                                          m_PreviewBoundsCenter,
                                                          m_PreviewCameraFovY,
                                                          aspect,
                                                          &*m_PreviewTarget.texture,
                                                          m_PreviewSkybox.valid());
                cam.worldOverride     = &m_PreviewWorld;
                cam.overrideFrameTime = true;
                cam.frameTimeSeconds  = m_PreviewTimeSeconds;
                cam.frameDeltaSeconds = previewDeltaTime;
                cameras->addManualCamera(cam);
            }
        }

        if (m_PreviewTarget.textureId)
            ImGui::Image(m_PreviewTarget.textureId, ImVec2(size, size));
        else
            ImGui::Dummy(ImVec2(size, size));
        const ImVec2 imageMin = ImGui::GetItemRectMin();
        const ImVec2 imageMax = ImGui::GetItemRectMax();
        ImGui::SetCursorScreenPos(imageMin);
        ImGui::InvisibleButton("##MaterialGraphPreviewInput", ImVec2(size, size), ImGuiButtonFlags_MouseButtonLeft);
        const bool previewHovered = ui::capturePreviewItemInput();
        ui::capturePreviewInput(m_PreviewArcballActive);
        if ((previewHovered || m_PreviewArcballActive) && !ImGui::GetIO().WantTextInput)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_F))
            {
                m_PreviewDistanceScale = 1.0f;
                focusPreviewMesh(ctx, aspect, false);
            }

            if (previewHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                m_PreviewArcballActive = true;
                m_PreviewArcballVector = mapPreviewArcballPoint(ImGui::GetIO().MousePos, imageMin, imageMax);
            }
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
                m_PreviewArcballActive = false;
            if (m_PreviewArcballActive && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
            {
                const glm::vec3 next = mapPreviewArcballPoint(ImGui::GetIO().MousePos, imageMin, imageMax);
                m_PreviewObjectRotation =
                    glm::normalize(arcballDelta(m_PreviewArcballVector, next) * m_PreviewObjectRotation);
                m_PreviewArcballVector = next;
            }

            const float wheel = ImGui::GetIO().MouseWheel;
            if (std::abs(wheel) > 0.0f)
            {
                m_PreviewFocusActive   = false;
                m_PreviewDistanceScale = std::clamp(m_PreviewDistanceScale * std::exp(-wheel * 0.16f), 0.08f, 8.0f);
            }
        }
        if (ui::drawMeshUuidField(ctx, vultra::tr("materialGraph.preview.mesh"), m_PreviewMesh, m_MeshSelector))
        {
            m_Status = m_PreviewMesh.valid() ? vultra::tr("materialGraph.status.previewMeshChanged") :
                                               vultra::tr("materialGraph.status.previewMeshReset");
            focusPreviewMesh(ctx, aspect, true);
        }
        if (ui::drawTextureUuidField(
                ctx, vultra::tr("materialGraph.preview.skybox"), m_PreviewSkybox, m_TextureSelector))
            m_Status = m_PreviewSkybox.valid() ? vultra::tr("materialGraph.status.previewSkyboxChanged") :
                                                 vultra::tr("materialGraph.status.previewSkyboxCleared");

        ImGui::SeparatorText(vultra::tr("materialGraph.preview.time"));
        if (ImGui::SmallButton(m_PreviewTimePlaying ? ICON_MDI_PAUSE : ICON_MDI_PLAY))
            m_PreviewTimePlaying = !m_PreviewTimePlaying;
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_MDI_RESTART))
            m_PreviewTimeSeconds = 0.0f; // replay from the start, keep playing
        ImGui::SameLine();
        ImGui::SetNextItemWidth(std::max(vultra::ui::dp(120.0f), size - vultra::ui::dp(82.0f)));
        if (ImGui::SliderFloat("##MaterialGraphPreviewTime", &m_PreviewTimeSeconds, 0.0f, 60.0f, "%.2f s"))
        {
            m_PreviewTimeSeconds = std::max(0.0f, m_PreviewTimeSeconds);
            m_PreviewTimePlaying = false;
        }

        ImGui::TextDisabled("%s",
                            m_PreviewMesh.valid() ? vultra::tr("materialGraph.preview.usingSelectedMesh") :
                                                    vultra::tr("materialGraph.preview.usingSphere"));
        ImGui::TextDisabled("%s", vultra::tr("materialGraph.preview.controlsHint"));
    }

    void MaterialGraphWindow::drawAddNodePopup(EditorContext& ctx)
    {
        if (!ImGui::BeginPopup("MaterialGraphAddNode"))
            return;

        static std::array<char, 128> search {};
        ImGui::SetNextItemWidth(vultra::ui::dp(260.0f));
        ImGui::InputTextWithHint(
            "##NodeSearch", vultra::tr("materialGraph.addNode.searchHint"), search.data(), search.size());
        ImGui::Separator();

        const auto addNodeItem = [&](const vultra::material_graph::NodeDescriptor& desc) {
            if (!ImGui::MenuItem(desc.displayName.c_str()))
                return;

            const std::string stem   = nodeIdStem(desc);
            std::string       unique = stem;
            int               suffix = 1;
            while (findNode(unique))
                unique = stem + std::to_string(++suffix);
            const ImVec2 panning = ImNodes::EditorContextGetPanning();
            const float  offset  = static_cast<float>(m_Graph.nodes.size() % 6u) * 32.0f;
            m_Graph.nodes.push_back(
                makeNode(desc, unique, ImVec2(-panning.x + 80.0f + offset, -panning.y + 80.0f + offset)));
            markDirty(ctx);
        };

        const auto drawNodeTypeItem = [&](const std::string& typeId) {
            const auto* desc = m_Registry.find(typeId);
            if (!desc)
                return;
            // A surface graph must contain exactly one output node, so disable adding
            // any output-family node once one already exists.
            if (vultra::material_graph::isSurfaceOutputType(typeId) &&
                std::ranges::any_of(m_Graph.nodes, [](const auto& node) {
                    return vultra::material_graph::isSurfaceOutputType(node.typeId);
                }))
            {
                ImGui::BeginDisabled();
                ImGui::MenuItem(desc->displayName.c_str());
                ImGui::EndDisabled();
                return;
            }
            addNodeItem(*desc);
        };

        static constexpr std::array categories {
            "Inputs",
            "Parameters",
            "Math",
            "Logic",
            "Texture",
            "Shading",
            "Output",
            "Other",
        };
        const auto categoryLabel = [](const char* category) -> const char* {
            const std::string_view name {category};
            if (name == "Inputs")
                return vultra::trId("materialGraph.category.inputs", "Inputs");
            if (name == "Parameters")
                return vultra::trId("materialGraph.category.parameters", "Parameters");
            if (name == "Math")
                return vultra::trId("materialGraph.category.math", "Math");
            if (name == "Logic")
                return vultra::trId("materialGraph.category.logic", "Logic");
            if (name == "Texture")
                return vultra::trId("materialGraph.category.texture", "Texture");
            if (name == "Shading")
                return vultra::trId("materialGraph.category.shading", "Shading");
            if (name == "Output")
                return vultra::trId("materialGraph.category.output", "Output");
            return vultra::trId("materialGraph.category.other", "Other");
        };
        const auto subcategoryLabel = [](const char* subcategory) -> const char* {
            const std::string_view name {subcategory};
            if (name == "Vertex Attributes")
                return vultra::trId("materialGraph.subcategory.vertexAttributes", "Vertex Attributes");
            if (name == "Textures")
                return vultra::trId("materialGraph.subcategory.textures", "Textures");
            return subcategory;
        };

        const auto typeIds = m_Registry.typeIds();
        if (search[0] != '\0')
        {
            int matched = 0;
            for (const auto& typeId : typeIds)
            {
                const auto* desc = m_Registry.find(typeId);
                if (!desc || !nodeMatchesSearch(*desc, search.data()))
                    continue;

                ++matched;
                drawNodeTypeItem(typeId);
            }
            if (matched == 0)
                ImGui::TextDisabled("%s", vultra::tr("materialGraph.addNode.noMatchingNodes"));
            ImGui::EndPopup();
            return;
        }

        for (const char* category : categories)
        {
            bool hasItems = false;
            for (const auto& typeId : typeIds)
            {
                if (nodeMenuCategory(typeId) == category)
                {
                    hasItems = true;
                    break;
                }
            }
            if (!hasItems)
                continue;

            if (ImGui::BeginMenu(categoryLabel(category)))
            {
                if (std::string_view(category) == "Inputs")
                {
                    static constexpr std::array subcategories {
                        "Vertex Attributes",
                        "Textures",
                    };
                    for (const char* subcategory : subcategories)
                    {
                        bool hasSubItems = false;
                        for (const auto& typeId : typeIds)
                        {
                            if (nodeMenuCategory(typeId) == category && nodeMenuSubcategory(typeId) == subcategory)
                            {
                                hasSubItems = true;
                                break;
                            }
                        }
                        if (!hasSubItems)
                            continue;
                        if (ImGui::BeginMenu(subcategoryLabel(subcategory)))
                        {
                            for (const auto& typeId : typeIds)
                            {
                                if (nodeMenuCategory(typeId) == category && nodeMenuSubcategory(typeId) == subcategory)
                                    drawNodeTypeItem(typeId);
                            }
                            ImGui::EndMenu();
                        }
                    }
                }
                else
                {
                    for (const auto& typeId : typeIds)
                    {
                        if (nodeMenuCategory(typeId) == category)
                            drawNodeTypeItem(typeId);
                    }
                }
                ImGui::EndMenu();
            }
        }
        ImGui::EndPopup();
    }

    void MaterialGraphWindow::markDirty(EditorContext& ctx)
    {
        m_Dirty          = true;
        m_HistoryPending = true; // a coalesced snapshot will be recorded once the edit settles
        if (auto* assets = ctx.services ? ctx.services->tryGet<vultra::IAssetService>() : nullptr)
            assets->setTextAssetOverride(m_CurrentUri, vultra::material_graph::saveGraphToText(m_Graph));
        m_Status = m_LiveApply ? vultra::tr("materialGraph.status.livePreviewUpdated") :
                                 vultra::tr("materialGraph.status.edited");
    }

    std::string MaterialGraphWindow::historySnapshot() const
    {
        return vultra::material_graph::saveGraphToText(m_Graph);
    }

    void MaterialGraphWindow::applyHistorySnapshot(EditorContext& ctx, const std::string& snapshot)
    {
        auto restored = vultra::material_graph::loadGraphFromText(snapshot);
        if (!restored)
            return;
        m_ApplyingHistory = true;
        m_Graph           = std::move(*restored);
        m_Pins.clear();            // rebuilt from the restored graph on the next draw
        markDirty(ctx);            // refresh the live-preview override; node positions reapply from JSON
        m_HistoryPending  = false; // the restored state itself must not be re-recorded
        m_ApplyingHistory = false;
    }

    void MaterialGraphWindow::undo(EditorContext& ctx) { m_History.undo(ctx); }

    void MaterialGraphWindow::redo(EditorContext& ctx) { m_History.redo(ctx); }

    void MaterialGraphWindow::ensurePreviewWorld(EditorContext&)
    {
        if (m_PreviewSphere != entt::null)
            return;
        auto& reg = m_PreviewWorld.registry();

        m_PreviewLight = m_PreviewWorld.createEntity();
        reg.emplace_or_replace<vultra::NameComponent>(m_PreviewLight, vultra::NameComponent {"Preview Key Light"});
        auto& lightTransform = reg.get<vultra::TransformComponent>(m_PreviewLight);
        lightTransform.rotation =
            glm::quatLookAt(glm::normalize(glm::vec3(0.4f, -0.8f, 0.35f)), glm::vec3(0.0f, 1.0f, 0.0f));
        lightTransform.worldMatrix = glm::mat4_cast(lightTransform.rotation);
        auto& l                    = reg.emplace_or_replace<vultra::LightComponent>(m_PreviewLight);
        l.kind                     = 0u;
        l.intensity                = 6.0f;
        l.castsShadow              = false;

        m_PreviewEnvironment = m_PreviewWorld.createEntity();
        reg.emplace_or_replace<vultra::NameComponent>(m_PreviewEnvironment,
                                                      vultra::NameComponent {"Preview Environment"});
        reg.emplace_or_replace<vultra::EnvironmentComponent>(m_PreviewEnvironment,
                                                             vultra::EnvironmentComponent {
                                                                 .ambientColor     = glm::vec3 {0.28f, 0.30f, 0.34f},
                                                                 .ambientIntensity = 1.6f,
                                                                 .enableIBL        = false,
                                                                 .iblColor         = glm::vec3 {0.45f, 0.48f, 0.52f},
                                                                 .iblIntensity     = 1.2f,
                                                             });

        m_PreviewSphere = m_PreviewWorld.createEntity();
        reg.emplace_or_replace<vultra::NameComponent>(m_PreviewSphere, vultra::NameComponent {"Preview Sphere"});
        reg.emplace_or_replace<vultra::MeshComponent>(
            m_PreviewSphere,
            vultra::MeshComponent {
                .builtinGeometry   = 2u,
                .materialOverrides = {{.slot = 0u, .materialGraph = m_CurrentUri}},
            });
    }

    void MaterialGraphWindow::ensurePreviewRenderTarget(EditorContext& ctx, uint32_t width, uint32_t height)
    {
        if (!ctx.services || width == 0u || height == 0u)
            return;
        collectRetiredPreviewTargets(ctx);
        if (m_PreviewTarget.texture && m_PreviewTarget.extent.width == width &&
            m_PreviewTarget.extent.height == height && m_PreviewTarget.textureId)
            return;
        releasePreviewRenderTarget(ctx);
        auto* backend = ctx.services->tryGet<vultra::IRenderBackendService>();
        auto* imgui   = ctx.services->tryGet<vultra::IImGuiService>();
        if (!backend || !imgui)
            return;
        auto format = backend->backbuffer().getPixelFormat();
        if (format == vultra::rhi::PixelFormat::eUndefined)
            format = vultra::rhi::PixelFormat::eRGBA8_UNorm;
        m_PreviewTarget.extent = {width, height};
        m_PreviewTarget.texture =
            vultra::rhi::Texture::Builder {}
                .setExtent(m_PreviewTarget.extent)
                .setPixelFormat(format)
                .setNumMipLevels(1)
                .setUsageFlags(vultra::rhi::ImageUsage::eRenderTarget | vultra::rhi::ImageUsage::eSampled |
                               vultra::rhi::ImageUsage::eTransferSrc)
                .build(backend->renderDevice());
        m_PreviewTarget.textureId = imgui->addTexture(*m_PreviewTarget.texture);
    }

    void MaterialGraphWindow::releasePreviewRenderTarget(EditorContext& ctx)
    {
        if (m_PreviewTarget.texture || !m_RetiredPreviewTargets.empty())
        {
            if (auto* backend = ctx.services ? ctx.services->tryGet<vultra::IRenderBackendService>() : nullptr)
                backend->renderDevice().waitIdle();
        }
        if (ctx.services)
            if (auto* imgui = ctx.services->tryGet<vultra::IImGuiService>())
                if (m_PreviewTarget.textureId)
                    imgui->removeTexture(m_PreviewTarget.textureId);
        m_PreviewTarget = {};
        m_RetiredPreviewTargets.releaseAll(ctx.services ? ctx.services->tryGet<vultra::IImGuiService>() : nullptr);
    }

    void MaterialGraphWindow::collectRetiredPreviewTargets(EditorContext& ctx)
    {
        const auto frame = static_cast<uint64_t>(ImGui::GetFrameCount());
        auto*      imgui = ctx.services ? ctx.services->tryGet<vultra::IImGuiService>() : nullptr;
        m_RetiredPreviewTargets.reclaim(imgui, frame);
    }

    int MaterialGraphWindow::nodeId(std::string_view id) const { return stableImNodesId(id); }

    int MaterialGraphWindow::pinId(std::string_view node, std::string_view pin, bool input)
    {
        const std::string key = std::string(input ? "in:" : "out:") + std::string(node) + ":" + std::string(pin);
        const int         id  = stableImNodesId(key);
        m_Pins[id]            = {std::string(node), std::string(pin), input};
        return id;
    }

    int MaterialGraphWindow::linkId(const vultra::material_graph::Link& link) const
    {
        const std::string key = link.from.nodeId + ":" + link.from.pin + "->" + link.to.nodeId + ":" + link.to.pin;
        return stableImNodesId(key);
    }

    vultra::material_graph::Node* MaterialGraphWindow::findNode(std::string_view id)
    {
        auto it = std::ranges::find_if(m_Graph.nodes, [&](const auto& node) { return node.id == id; });
        return it == m_Graph.nodes.end() ? nullptr : &*it;
    }

    const vultra::material_graph::Node* MaterialGraphWindow::findNode(std::string_view id) const
    {
        auto it = std::ranges::find_if(m_Graph.nodes, [&](const auto& node) { return node.id == id; });
        return it == m_Graph.nodes.end() ? nullptr : &*it;
    }

    void MaterialGraphWindow::ensureNodePorts(vultra::material_graph::Node& node)
    {
        const auto* desc = m_Registry.find(node.typeId);
        if (!desc)
            return;
        node.displayName = node.displayName.empty() ? desc->displayName : node.displayName;
        node.inputs      = desc->inputs;
        node.outputs     = desc->outputs;
        for (const auto& [key, value] : desc->defaultParams.items())
            if (!node.params.contains(key))
                node.params[key] = value;
        for (const auto& input : desc->inputs)
            if (input.defaultValue && !node.params.contains(input.name))
                node.params[input.name] = *input.defaultValue;
    }
} // namespace vultra_app
