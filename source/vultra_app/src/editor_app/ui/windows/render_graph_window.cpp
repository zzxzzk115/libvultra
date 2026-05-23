#include "editor_app/ui/windows/render_graph_window.hpp"

#include <vultra/function/rendering/render_structs.hpp>
#include <vultra/function/services/asset_service.hpp>
#include <vultra/function/services/camera_service.hpp>
#include <vultra/function/services/imgui_service.hpp>
#include <vultra/function/services/render_backend_service.hpp>
#include <vultra/function/services/render_service.hpp>
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
#include <imnodes/imnodes.h>
#include <nlohmann/json.hpp>

#include <entt/entity/entity.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
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

        int stableId(std::string_view text)
        {
            return static_cast<int>(fnv1a32(text) & 0x7fffffffu);
        }

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
            return std::any_of(graph.resources.begin(), graph.resources.end(), [&](const auto& r) { return r.name == name; });
        }

        void ensureSlots(vrendergraph::PassDecl& pass, const vrendergraph::PassDefinition& def)
        {
            for (const auto& slot : def.inputs)
                pass.inputs.try_emplace(slot, "");
            for (const auto& slot : def.outputs)
                pass.outputs.try_emplace(slot, pass.id + "." + slot);
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
            std::unordered_map<std::string, int> indegree;
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
                std::sort(ready.begin(), ready.end(), [&](const auto& a, const auto& b) { return order[a] < order[b]; });
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

        struct ParamEnumOption
        {
            std::string label;
            int         value {0};
        };

        std::vector<ParamEnumOption> readShaderParamEnum(const EditorContext& ctx,
                                                         const vrendergraph::PassDecl& pass,
                                                         std::string_view paramName);

        void drawParamField(EditorContext& ctx, vrendergraph::PassDecl& pass, const vrendergraph::ParamDesc& param, bool& dirty)
        {
            if (param.name == "name" || param.name == "library" || param.name == "vertex" || param.name == "fragment" ||
                param.name == "publish" || param.name == "pushConstants")
                return;

            auto& raw = pass.params.raw();
            if (!raw.contains(param.name))
                raw[param.name] = param.defaultValue;

            ImGui::PushID(param.name.c_str());
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(param.name.c_str());
            ImGui::SameLine(92.0f);
            ImGui::PushItemWidth(190.0f);

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
                    dirty = true;
                }
            }
            else if (param.type == vrendergraph::ParamType::eInt)
            {
                int value = raw[param.name].get<int>();
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
                                dirty = true;
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
                    dirty = true;
                }
            }
            else if (param.type == vrendergraph::ParamType::eBoolean)
            {
                bool value = raw[param.name].get<bool>();
                if (ImGui::Checkbox("##value", &value))
                {
                    raw[param.name] = value;
                    dirty = true;
                }
            }
            else
            {
                std::array<char, 256> buffer {};
                const auto            value = raw[param.name].get<std::string>();
                std::snprintf(buffer.data(), buffer.size(), "%s", value.c_str());
                if (ImGui::InputText("##value", buffer.data(), buffer.size()))
                {
                    raw[param.name] = std::string(buffer.data());
                    dirty = true;
                }
            }

            ImGui::PopItemWidth();
            ImGui::PopID();
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
            hovered.x = std::min(hovered.x + 0.08f, 1.0f);
            hovered.y = std::min(hovered.y + 0.08f, 1.0f);
            hovered.z = std::min(hovered.z + 0.08f, 1.0f);

            ImVec4 selected = ImGui::ColorConvertU32ToFloat4(title);
            selected.x = std::min(selected.x + 0.15f, 1.0f);
            selected.y = std::min(selected.y + 0.15f, 1.0f);
            selected.z = std::min(selected.z + 0.15f, 1.0f);

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
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            ImFont* font = ImGui::GetFont();
            const ImU32 outline = IM_COL32(0, 0, 0, 220);
            const ImU32 main = ImGui::GetColorU32(ImGuiCol_Text);

            drawList->AddText(font, fontSize, ImVec2(pos.x - 1.0f, pos.y), outline, text);
            drawList->AddText(font, fontSize, ImVec2(pos.x + 1.0f, pos.y), outline, text);
            drawList->AddText(font, fontSize, ImVec2(pos.x, pos.y - 1.0f), outline, text);
            drawList->AddText(font, fontSize, ImVec2(pos.x, pos.y + 1.0f), outline, text);
            drawList->AddText(font, fontSize, pos, main, text);

            const float scale = fontSize / ImGui::GetFontSize();
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
            size_t end = openBrace;
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

                const char quote = block[i++];
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

        std::string trim(std::string_view text)
        {
            const auto first = text.find_first_not_of(" \t\r\n");
            if (first == std::string_view::npos)
                return {};
            const auto last = text.find_last_not_of(" \t\r\n");
            return std::string(text.substr(first, last - first + 1));
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

            const auto root = (ctx.state.currentProject / ctx.state.currentAssetRoot / "shaders").lexically_normal();
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

        std::vector<ParamEnumOption> readShaderParamEnum(const EditorContext& ctx,
                                                         const vrendergraph::PassDecl& pass,
                                                         std::string_view paramName)
        {
            const auto fragment = pass.params.get<std::string>("fragment", {});
            const auto shaderPath = findShaderSourceFile(ctx, fragment);
            if (!shaderPath)
                return {};

            std::ifstream file(*shaderPath);
            if (!file.is_open())
                return {};

            const std::string prefix = "@param_enum " + std::string(paramName);
            std::string line;
            while (std::getline(file, line))
            {
                const auto marker = line.find(prefix);
                if (marker == std::string::npos)
                    continue;

                std::vector<ParamEnumOption> options;
                std::string rest = trim(std::string_view(line).substr(marker + prefix.size()));
                std::stringstream ss(rest);
                std::string item;
                while (std::getline(ss, item, ','))
                {
                    item = trim(item);
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

            const auto local = makeTransformMatrix(*transform);
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

            return glm::perspectiveRH_ZO(glm::radians(camera.fovYDegrees),
                                         std::max(aspect, 0.0001f),
                                         zNear,
                                         zFar);
        }

        entt::entity findPrimaryCamera(vultra::World& world)
        {
            auto& reg  = world.registry();
            auto  view = reg.view<vultra::IDComponent, vultra::TransformComponent, vultra::CameraComponent>();

            entt::entity best = entt::null;
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

        vultra::RenderCamera makeOverlayGameCamera(vultra::World&     world,
                                                   const entt::entity entity,
                                                   const float        aspect,
                                                   vultra::rhi::Texture* target)
        {
            auto& reg       = world.registry();
            auto& id        = reg.get<vultra::IDComponent>(entity);
            auto& camera    = reg.get<vultra::CameraComponent>(entity);

            vultra::RenderCamera out {};
            out.uuid        = id.uuid;
            out.name        = "Render Graph Overlay";
            out.priority    = camera.priority;
            out.view        = glm::inverse(makeWorldTransformMatrix(reg, entity));
            out.projection  = makeGameProjection(camera, aspect);
            out.zNear       = std::max(camera.zNear, 0.0001f);
            out.zFar        = std::max(camera.zFar, out.zNear + 0.0001f);
            out.fovY        = glm::radians(camera.fovYDegrees);
            out.target      = target;
            out.clearValue  = camera.clearColor;
            out.clearValue.a = 1.0f;
            out.renderImGui = false;
            out.rendererKey = camera.rendererKey.empty() || camera.rendererKey == "universal" ? "project" : camera.rendererKey;
            return out;
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
            int         layer {0};
            int         row {0};
        };

        ImNodesEditorContext* editorContext {nullptr};
        std::vector<Node>        nodes;
        std::vector<Edge>        edges;
        std::vector<std::string> graphLabels;
        std::vector<std::string> graphKeys;
        std::string              selectedGraphKey;
        int                      selectedGraphIndex {-1};
        std::string              camera;
        std::string              cameraDisplay;
        std::string              renderer;
        std::string              diagnostics;
        std::unordered_map<std::string, int> ids;
        int                      nextId {1};
        size_t                   snapshotHash {0};
        bool                     applyLayout {false};

        RuntimeGraphState()
        {
            editorContext = ImNodes::EditorContextCreate();
        }

        ~RuntimeGraphState()
        {
            if (editorContext)
                ImNodes::EditorContextFree(editorContext);
        }

        int idFor(std::string key)
        {
            if (auto it = ids.find(key); it != ids.end())
                return it->second;
            const int id = nextId++;
            ids.emplace(std::move(key), id);
            return id;
        }

        int nodeId(std::string_view name)
        {
            return idFor("runtime:node:" + std::string(name));
        }

        int inputPinId(std::string_view name)
        {
            return idFor("runtime:in:" + std::string(name));
        }

        int outputPinId(std::string_view name)
        {
            return idFor("runtime:out:" + std::string(name));
        }

        int linkId(const Edge& edge)
        {
            return idFor("runtime:link:" + edge.from + "->" + edge.to + ":" + edge.label);
        }

        void computeLayout()
        {
            std::unordered_map<std::string, size_t> index;
            index.reserve(nodes.size());
            for (size_t i = 0; i < nodes.size(); ++i)
                index[nodes[i].id] = i;

            std::vector<std::vector<size_t>> outgoing(nodes.size());
            std::vector<int>                 indegree(nodes.size(), 0);
            for (const auto& edge : edges)
            {
                auto fromIt = index.find(edge.from);
                auto toIt = index.find(edge.to);
                if (fromIt == index.end() || toIt == index.end())
                    continue;

                outgoing[fromIt->second].push_back(toIt->second);
                ++indegree[toIt->second];
            }

            std::vector<size_t> ready;
            for (size_t i = 0; i < nodes.size(); ++i)
            {
                if (indegree[i] == 0)
                    ready.push_back(i);
            }

            auto stableNodeLess = [&](const size_t a, const size_t b) {
                const auto rank = [](const Node& node) {
                    if (node.kind == "pass")
                        return 0;
                    if (node.imported)
                        return 1;
                    if (node.kind == "resource")
                        return 2;
                    return 3;
                };
                if (rank(nodes[a]) != rank(nodes[b]))
                    return rank(nodes[a]) < rank(nodes[b]);
                if (nodes[a].kind != nodes[b].kind)
                    return nodes[a].kind < nodes[b].kind;
                return nodes[a].label < nodes[b].label;
            };

            size_t visited = 0;
            while (!ready.empty())
            {
                std::sort(ready.begin(), ready.end(), stableNodeLess);
                const size_t current = ready.front();
                ready.erase(ready.begin());
                ++visited;

                for (const auto next : outgoing[current])
                {
                    nodes[next].layer = std::max(nodes[next].layer, nodes[current].layer + 1);
                    if (--indegree[next] == 0)
                        ready.push_back(next);
                }
            }

            if (visited != nodes.size())
            {
                for (size_t i = 0; i < nodes.size(); ++i)
                    nodes[i].layer = static_cast<int>(i / 6);
            }

            std::unordered_map<int, std::vector<size_t>> layers;
            for (size_t i = 0; i < nodes.size(); ++i)
                layers[nodes[i].layer].push_back(i);

            for (auto& [layer, layerNodes] : layers)
            {
                (void)layer;
                std::sort(layerNodes.begin(), layerNodes.end(), stableNodeLess);
                for (size_t row = 0; row < layerNodes.size(); ++row)
                    nodes[layerNodes[row]].row = static_cast<int>(row);
            }
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
            renderer.clear();
            diagnostics.clear();
            ids.clear();
            nextId = 1;

            std::unordered_map<std::string, Node> nodeById;
            auto addNode = [&](std::string id, std::string label = {}, std::string kind = {}) {
                id = trim(id);
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
            size_t lineStart = 0;
            while (lineStart < snapshot.size())
            {
                const size_t lineEnd = snapshot.find('\n', lineStart);
                const auto   line = snapshot.substr(lineStart, lineEnd == std::string_view::npos ? std::string_view::npos : lineEnd - lineStart);
                const auto   trimmedLine = trim(line);
                if (!trimmedLine.empty())
                {
                    try
                    {
                        const auto json = nlohmann::json::parse(trimmedLine);
                        allGraphEntries.push_back(json);
                    }
                    catch (const std::exception&)
                    {
                    }
                }

                if (lineEnd == std::string_view::npos)
                    break;
                lineStart = lineEnd + 1;
            }

            std::vector<nlohmann::json> graphEntries;
            graphEntries.reserve(allGraphEntries.size());
            for (const auto& json : allGraphEntries)
            {
                const auto cameraName = json.value("camera", std::string {});
                if (cameraName == "Vultra Editor UI")
                    continue;
                graphEntries.push_back(json);
            }
            if (graphEntries.empty())
                graphEntries = std::move(allGraphEntries);

            int projectGraphIndex = -1;
            int gameGraphIndex = -1;
            int largestGraphIndex = -1;
            size_t largestGraphSize = 0;
            for (size_t i = 0; i < graphEntries.size(); ++i)
            {
                const auto& json = graphEntries[i];
                const auto  rendererKey = json.value("renderer", std::string {"renderer?"});
                const auto  cameraName = json.value("camera", std::string {"camera?"});
                const auto  nodesJson = json.value("nodes", nlohmann::json::array());
                const auto  nodeCount = nodesJson.size();
                auto        graphKey = rendererKey + "/" + cameraName;
                if (std::find(graphKeys.begin(), graphKeys.end(), graphKey) != graphKeys.end())
                    graphKey += "#" + std::to_string(i);

                graphKeys.push_back(graphKey);
                graphLabels.push_back(runtimeCameraDisplayName(cameraName) + " - " + rendererKey + " (" +
                                      std::to_string(nodeCount) + " nodes)");

                if (projectGraphIndex < 0 && rendererKey == "project")
                    projectGraphIndex = static_cast<int>(i);
                if (gameGraphIndex < 0 &&
                    (cameraName.find("Game") != std::string::npos || cameraName.find("game") != std::string::npos))
                    gameGraphIndex = static_cast<int>(i);
                if (nodeCount > largestGraphSize)
                {
                    largestGraphSize = nodeCount;
                    largestGraphIndex = static_cast<int>(i);
                }
            }

            const auto selectedIt = std::find(graphKeys.begin(), graphKeys.end(), selectedGraphKey);
            if (selectedIt != graphKeys.end())
            {
                selectedGraphIndex = static_cast<int>(std::distance(graphKeys.begin(), selectedIt));
            }
            else if (projectGraphIndex >= 0)
            {
                selectedGraphIndex = projectGraphIndex;
                selectedGraphKey = graphKeys[static_cast<size_t>(selectedGraphIndex)];
            }
            else if (gameGraphIndex >= 0)
            {
                selectedGraphIndex = gameGraphIndex;
                selectedGraphKey = graphKeys[static_cast<size_t>(selectedGraphIndex)];
            }
            else if (largestGraphIndex >= 0)
            {
                selectedGraphIndex = largestGraphIndex;
                selectedGraphKey = graphKeys[static_cast<size_t>(selectedGraphIndex)];
            }
            else if (!graphLabels.empty())
            {
                selectedGraphIndex = 0;
                selectedGraphKey = graphKeys.front();
            }

            if (!graphEntries.empty() && selectedGraphIndex >= 0 &&
                selectedGraphIndex < static_cast<int>(graphEntries.size()))
            {
                const auto& selectedGraph = graphEntries[static_cast<size_t>(selectedGraphIndex)];
                camera = selectedGraph.value("camera", std::string {});
                cameraDisplay = runtimeCameraDisplayName(camera);
                renderer = selectedGraph.value("renderer", std::string {});
                const auto selectedNodesJson = selectedGraph.value("nodes", nlohmann::json::array());
                const auto selectedEdgesJson = selectedGraph.value("edges", nlohmann::json::array());
                size_t     invalidEdges = 0;
                size_t     implicitNodes = 0;

                for (const auto& node : selectedNodesJson)
                {
                    const auto id = node.value("id", std::string {});
                    addNode(id,
                            node.value("label", std::string {}),
                            node.value("kind", std::string {}));
                    if (auto it = nodeById.find(id); it != nodeById.end())
                    {
                        it->second.imported = node.value("imported", false);
                        it->second.active = node.value("active", true);
                        it->second.sideEffect = node.value("sideEffect", false);
                        it->second.version = node.value("version", 0);
                    }
                }
                for (const auto& edge : selectedEdgesJson)
                {
                    const auto label = edge.value("label", std::string {});
                    if (edge.contains("from") && edge.contains("to"))
                    {
                        const auto from = edge.value("from", std::string {});
                        const auto to = edge.value("to", std::string {});
                        if (from.empty() || to.empty() || from == to)
                        {
                            ++invalidEdges;
                            continue;
                        }
                        if (!nodeById.contains(from))
                        {
                            ++implicitNodes;
                            addNode(from);
                        }
                        if (!nodeById.contains(to))
                        {
                            ++implicitNodes;
                            addNode(to);
                        }
                        edges.push_back({from, to, label});
                    }
                    else if (edge.contains("vertices") && edge["vertices"].is_array())
                    {
                        const auto& vertices = edge["vertices"];
                        for (size_t i = 1; i < vertices.size(); ++i)
                        {
                            const auto from = vertices[i - 1].get<std::string>();
                            const auto to = vertices[i].get<std::string>();
                            if (from.empty() || to.empty() || from == to)
                            {
                                ++invalidEdges;
                                continue;
                            }
                            if (!nodeById.contains(from))
                            {
                                ++implicitNodes;
                                addNode(from);
                            }
                            if (!nodeById.contains(to))
                            {
                                ++implicitNodes;
                                addNode(to);
                            }
                            edges.push_back({from, to, label});
                        }
                    }
                    else
                    {
                        ++invalidEdges;
                    }
                }

                std::ostringstream diag;
                diag << "snapshot entries=" << graphEntries.size() << ", source nodes=" << selectedNodesJson.size()
                     << ", source edges=" << selectedEdgesJson.size() << ", implicit nodes=" << implicitNodes
                     << ", invalid edges=" << invalidEdges;
                diagnostics = diag.str();
            }

            nodes.reserve(nodeById.size());
            for (auto& [id, node] : nodeById)
                nodes.push_back(std::move(node));
            std::unordered_set<std::string> nodeIds;
            nodeIds.reserve(nodes.size());
            for (const auto& node : nodes)
                nodeIds.insert(node.id);
            std::erase_if(edges, [&](const auto& edge) {
                return !nodeIds.contains(edge.from) || !nodeIds.contains(edge.to);
            });
            computeLayout();
            std::sort(nodes.begin(), nodes.end(), [](const auto& a, const auto& b) {
                if (a.layer != b.layer)
                    return a.layer < b.layer;
                if (a.row != b.row)
                    return a.row < b.row;
                return a.label < b.label;
            });
            applyLayout = true;
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
        std::filesystem::path             path;
        std::filesystem::path             pipelinePath;
        std::vector<std::string>          pipelineFeatures;
        std::unordered_map<int, Pin>      pins;
        std::string                       status;
        int                               contextNode {0};
        int                               contextLink {0};
        bool                              loaded {false};
        bool                              dirty {false};
        bool                              runtimeDirty {false};
        bool                              applyPositions {false};
        bool                              liveApply {true};

        GraphEditorState()
        {
            editorContext = ImNodes::EditorContextCreate();

            registry.registerResource("final_composition_source");
            registry.registerResource("color");
            registry.registerResource("camera_color");
            registry.registerResource("depth");
            registry.registerResource("backbuffer");
            registry.registerResource("target");

            registry.registerPass(vrendergraph::PassDefinition {
                .type = "FullscreenShader",
                .setup = [](FrameGraph&, FrameGraphBlackboard&, const vrendergraph::ParamBlock&, vrendergraph::PassBuildContext&) {},
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

        ~GraphEditorState()
        {
            if (editorContext)
                ImNodes::EditorContextFree(editorContext);
        }

        int nodeId(std::string_view kind, std::string_view id) const
        {
            return stableId(std::string(kind) + ":" + std::string(id));
        }

        int pinId(std::string_view node, std::string_view slot, bool input)
        {
            const int id = stableId(std::string(input ? "in:" : "out:") + std::string(node) + ":" + std::string(slot));
            pins[id] = Pin {std::string(node), std::string(slot), input};
            return id;
        }

        int linkId(std::string_view ref, std::string_view pass, std::string_view slot) const
        {
            return stableId(std::string("link:") + std::string(ref) + "->" + std::string(pass) + "." + std::string(slot));
        }

        int flowLinkId(std::string_view from, std::string_view to) const
        {
            return stableId(std::string("flow:") + std::string(from) + "->" + std::string(to));
        }

        std::string metaKeyForResource(std::string_view name) const
        {
            return "resource:" + std::string(name);
        }

        bool isCurrentGraphFeature(std::string_view feature) const
        {
            if (path.empty())
                return false;
            const auto generic = path.generic_string();
            const auto pos = generic.find("/resources/");
            const auto resPath = pos == std::string::npos ? generic : generic.substr(pos + 11);
            return feature == "res://" + resPath || feature == resPath || feature.ends_with(path.filename().generic_string());
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
            const ImVec2 pos = ImNodes::GetNodeGridSpacePos(id);
            nodes[std::string(key)]["pos"] = nlohmann::json::array({pos.x, pos.y});
        }

        void storeMeta()
        {
            ImNodes::EditorContextSet(editorContext);
            for (const auto& resource : graph.resources)
                storeNodePos(metaKeyForResource(resource.name), nodeId("resource_in", resource.name));
            for (const auto& pass : graph.passes)
                storeNodePos(pass.id, nodeId("pass", pass.id));
        }

        void markDirty()
        {
            dirty        = true;
            runtimeDirty = true;
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
                if (pass.params.has("publish") && !hasResource(graph, pass.params.get<std::string>("publish", {})))
                    pass.params.raw()["publish"] = "";
            }
            markDirty();
            status = "Deleted resource";
            return true;
        }

        bool removeSelected()
        {
            ImNodes::EditorContextSet(editorContext);
            bool removed = false;
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

    void RenderGraphWindow::onDestroy(EditorContext& ctx) { releaseOverlayRenderTarget(ctx); }

    void RenderGraphWindow::draw(EditorContext& ctx)
    {
        resetOverlayRenderTargetForProject(ctx);

        ImGuiWindowFlags windowFlags = ImGuiWindowFlags_None;
        if (m_GraphEditor && m_GraphEditor->dirty)
            windowFlags |= ImGuiWindowFlags_UnsavedDocument;

        const bool visible = ImGui::Begin(title().c_str(), &m_Open, windowFlags);
        if (!visible)
        {
            ImGui::End();
            return;
        }

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Mode");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140.0f);
        const char* modeLabel = m_Mode == Mode::ePreview ? "Preview" : "Edit";
        if (ImGui::BeginCombo("##RenderGraphMode", modeLabel))
        {
            if (ImGui::Selectable("Preview", m_Mode == Mode::ePreview))
                m_Mode = Mode::ePreview;
            if (ImGui::Selectable("Edit", m_Mode == Mode::eEdit))
                m_Mode = Mode::eEdit;
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s",
                            m_Mode == Mode::ePreview ?
                                "Runtime frame graph for the selected camera" :
                                "Project render graph asset");
        ImGui::Separator();

        switch (m_Mode)
        {
            case Mode::ePreview:
                drawRuntimeGraph(ctx);
                break;
            case Mode::eEdit:
                drawGraphEditor(ctx);
                break;
        }

        ImGui::End();
    }

    void RenderGraphWindow::drawRuntimeGraph(EditorContext& ctx)
    {
        if (!m_RuntimeGraph)
            m_RuntimeGraph = std::make_unique<RuntimeGraphState>();

        auto* renderService = ctx.services ? ctx.services->tryGet<vultra::IRenderService>() : nullptr;
        const std::string snapshot = renderService ? std::string(renderService->lastFrameGraphSnapshot()) : std::string {};
        auto&             graph = *m_RuntimeGraph;
        graph.parse(snapshot);

        if (!graph.diagnostics.empty())
            ImGui::TextDisabled("(%s)", graph.diagnostics.c_str());

        if (!graph.graphLabels.empty())
        {
            if (!graph.diagnostics.empty())
                ImGui::SameLine();
            ImGui::SetNextItemWidth(240.0f);
            const char* preview = graph.graphLabels[static_cast<size_t>(
                std::clamp(graph.selectedGraphIndex, 0, static_cast<int>(graph.graphLabels.size()) - 1))].c_str();
            if (ImGui::BeginCombo("##RuntimeGraphCamera", preview))
            {
                for (int i = 0; i < static_cast<int>(graph.graphLabels.size()); ++i)
                {
                    const bool selected = i == graph.selectedGraphIndex;
                    if (ImGui::Selectable(graph.graphLabels[static_cast<size_t>(i)].c_str(), selected))
                    {
                        graph.selectedGraphIndex = i;
                        graph.selectedGraphKey = graph.graphKeys[static_cast<size_t>(i)];
                        graph.snapshotHash = 0;
                        graph.parse(snapshot);
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }

        ImGui::Separator();
        if (snapshot.empty())
        {
            ImGui::TextDisabled("No frame graph has been compiled yet.");
            return;
        }

        ImGui::BeginChild("##RuntimeFrameGraphNodes",
                          ImGui::GetContentRegionAvail(),
                          true,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImNodes::EditorContextSet(graph.editorContext);
        ImNodes::BeginNodeEditor();

        constexpr float nodeWidth = 250.0f;
        constexpr float layerSpacing = 380.0f;
        constexpr float rowSpacing = 150.0f;
        for (size_t i = 0; i < graph.nodes.size(); ++i)
        {
            const auto& node = graph.nodes[i];
            const int   nodeId = graph.nodeId(node.id);
            const bool  resourceNode = node.kind == "resource";
            const ImU32 titleColor = resourceNode ?
                                         (node.imported ? IM_COL32(84, 122, 176, 255) : IM_COL32(70, 138, 148, 255)) :
                                         (node.sideEffect ? IM_COL32(172, 118, 58, 255) : IM_COL32(86, 136, 82, 255));
            const ImU32 bodyColor = resourceNode ?
                                        (node.imported ? IM_COL32(26, 34, 48, 255) : IM_COL32(24, 42, 44, 255)) :
                                        (node.sideEffect ? IM_COL32(48, 37, 24, 255) : IM_COL32(29, 42, 29, 255));
            pushNodePalette(titleColor, bodyColor);
            ImNodes::BeginNode(nodeId);
            ImNodes::BeginNodeTitleBar();
            ImGui::TextUnformatted(node.label.c_str());
            ImNodes::EndNodeTitleBar();

            ImNodes::BeginInputAttribute(graph.inputPinId(node.id), ImNodesPinShape_CircleFilled);
            ImGui::TextDisabled(resourceNode ? (node.imported ? "import" : "read") : "in");
            ImNodes::EndInputAttribute();
            ImNodes::BeginOutputAttribute(graph.outputPinId(node.id), ImNodesPinShape_CircleFilled);
            ImGui::Indent(nodeWidth - 42.0f);
            ImGui::TextDisabled(resourceNode ? "use" : (node.sideEffect ? "side" : "out"));
            ImGui::Unindent(nodeWidth - 42.0f);
            ImNodes::EndOutputAttribute();
            if (resourceNode && node.imported)
                ImGui::TextDisabled("imported");
            else if (!resourceNode && node.sideEffect)
                ImGui::TextDisabled("side effect");
            ImNodes::EndNode();
            popNodePalette();

            if (graph.applyLayout)
            {
                ImNodes::SetNodeGridSpacePos(nodeId,
                                             ImVec2 {static_cast<float>(node.layer) * layerSpacing,
                                                     static_cast<float>(node.row) * rowSpacing});
            }
        }

        for (const auto& edge : graph.edges)
        {
            const ImU32 color = edge.label == "read" ? IM_COL32(82, 154, 206, 220) :
                                edge.label == "write" ? IM_COL32(118, 190, 116, 230) :
                                                        IM_COL32(170, 170, 170, 210);
            pushLinkPalette(color);
            ImNodes::Link(graph.linkId(edge), graph.outputPinId(edge.from), graph.inputPinId(edge.to));
            popLinkPalette();
        }

        ImNodes::MiniMap(0.18f, ImNodesMiniMapLocation_BottomRight);
        ImNodes::EndNodeEditor();
        graph.applyLayout = false;
        ImGui::EndChild();
    }

    void RenderGraphWindow::drawGraphEditor(EditorContext& ctx)
    {
        if (!m_GraphEditor)
            m_GraphEditor = std::make_unique<GraphEditorState>();

        auto& state = *m_GraphEditor;
        const auto graphPath = ctx.state.currentProject.empty() ?
                                   std::filesystem::path {} :
                                   (ctx.state.currentProject / ctx.state.currentAssetRoot / "render/graphs/tonemapping.vrg.json")
                                       .lexically_normal();
        const auto pipelinePath = ctx.state.currentProject.empty() ?
                                      std::filesystem::path {} :
                                      (ctx.state.currentProject / ctx.state.currentAssetRoot / "render/default.vsrp.lua")
                                          .lexically_normal();
        auto graphUri = [&]() {
            if (ctx.state.currentProject.empty() || state.path.empty())
                return std::string {};
            std::error_code ec;
            const auto assetRoot = (ctx.state.currentProject / ctx.state.currentAssetRoot).lexically_normal();
            const auto rel = std::filesystem::relative(state.path, assetRoot, ec);
            if (ec || rel.empty())
                return std::string {};
            return "res://" + rel.generic_string();
        };
        auto serializedGraph = [&]() {
            state.storeMeta();
            return vrendergraph::saveRenderGraph(state.graph).dump(2);
        };
        auto applyGraphToRuntime = [&]() {
            const auto uri = graphUri();
            if (uri.empty())
                return false;

            auto* assetService = ctx.services ? ctx.services->tryGet<vultra::IAssetService>() : nullptr;
            auto* renderService = ctx.services ? ctx.services->tryGet<vultra::IRenderService>() : nullptr;
            if (!assetService || !renderService)
            {
                state.status = "Runtime apply failed: render/asset service unavailable.";
                return false;
            }

            assetService->setTextAssetOverride(uri, serializedGraph());
            renderService->reloadRenderPipeline();
            state.runtimeDirty = false;
            state.status = "Applied in memory";
            ctx.state.statusMessage = "Applied render graph in memory: " + uri;
            return true;
        };
        auto persistGraph = [&]() {
            const auto  text = serializedGraph();
            std::string error;

            if (!writeTextAtomic(state.path, text, error))
            {
                state.status = error;
                ctx.state.statusMessage = error;
                return false;
            }

            state.dirty = false;
            state.runtimeDirty = false;
            state.status = "Saved";
            ctx.state.statusMessage = "Saved render graph: " + state.path.generic_string();

            if (auto* assetService = ctx.services ? ctx.services->tryGet<vultra::IAssetService>() : nullptr)
            {
                const auto uri = graphUri();
                if (!uri.empty())
                    assetService->clearTextAssetOverride(uri);
            }
            if (auto* renderService = ctx.services ? ctx.services->tryGet<vultra::IRenderService>() : nullptr)
                renderService->reloadRenderPipeline();
            return true;
        };

        if (!graphPath.empty() && graphPath != state.path)
        {
            state.path = graphPath;
            state.pipelinePath = pipelinePath;
            state.loaded = false;
            state.dirty = false;
            state.runtimeDirty = false;
            state.status.clear();
        }
        else if (!pipelinePath.empty() && pipelinePath != state.pipelinePath)
        {
            state.pipelinePath = pipelinePath;
            state.loaded = false;
        }

        if (ImGui::Button(ICON_MDI_PLUS " Add"))
            ImGui::OpenPopup("RenderGraphAddMenu");
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

        const bool canSave = state.loaded && !state.path.empty();
        if (!canSave)
            ImGui::BeginDisabled();
        if (ImGui::SmallButton(ICON_MDI_CONTENT_SAVE " Save"))
        {
            persistGraph();
        }
        if (!canSave)
            ImGui::EndDisabled();

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
        if (ImGui::SmallButton(ICON_MDI_DELETE " Delete Selected") && state.loaded)
            state.removeSelected();

        ImGui::TextDisabled("%s%s",
                            state.path.empty() ? "No project graph selected" : state.path.generic_string().c_str(),
                            state.dirty ? " *" : "");
        if (!state.status.empty())
        {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", state.status.c_str());
        }

        ImGui::Separator();

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
                    for (auto& pass : state.graph.passes)
                    {
                        if (state.registry.contains(pass.type))
                            ensureSlots(pass, state.registry.get(pass.type));
                    }
                    state.pipelineFeatures.clear();
                    std::ifstream pipelineFile(state.pipelinePath);
                    if (pipelineFile.is_open())
                    {
                        const std::string pipelineText((std::istreambuf_iterator<char>(pipelineFile)),
                                                       std::istreambuf_iterator<char>());
                        state.pipelineFeatures = parsePipelineFeatureRefs(pipelineText);
                    }
                    state.loaded = true;
                    state.dirty = false;
                    state.runtimeDirty = false;
                    state.applyPositions = true;
                    state.status = "Loaded";
                }
                catch (const std::exception& e)
                {
                    ImGui::TextColored(ImVec4 {1.0f, 0.35f, 0.25f, 1.0f}, "Failed to load graph: %s", e.what());
                }
            }
        }

        if (!state.loaded)
        {
            ImGui::TextDisabled("Open a project with resources/render/graphs/tonemapping.vrg.json.");
            return;
        }

        drawGraphEditorCanvas(ctx);

        if (!ImGui::GetIO().WantTextInput && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
            ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S))
            persistGraph();

        if (state.liveApply && state.runtimeDirty && !state.path.empty())
            applyGraphToRuntime();
    }

    void RenderGraphWindow::drawGraphEditorAddPopup(EditorContext&)
    {
        auto& state = *m_GraphEditor;

        if (!ImGui::BeginPopup("RenderGraphAddMenu"))
            return;

        if (ImGui::BeginMenu("Graph Resource"))
        {
            auto names = state.registry.listResources();
            std::sort(names.begin(), names.end());
            for (const auto& name : names)
            {
                const bool exists = hasResource(state.graph, name);
                if (ImGui::MenuItem(name.c_str(), nullptr, false, !exists))
                {
                    state.graph.resources.push_back({name});
                    state.markDirty();
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Custom Graph Pass"))
        {
            if (ImGui::MenuItem("Tonemapping"))
            {
                const auto& def = state.registry.get("FullscreenShader");
                int         suffix = 1;
                std::string id = "CustomTonemapping";
                while (findPass(state.graph, id))
                    id = "CustomTonemapping_" + std::to_string(suffix++);

                vrendergraph::PassDecl pass;
                pass.id = std::move(id);
                pass.type = "FullscreenShader";
                ensureSlots(pass, def);
                pass.inputs["source"] = state.graph.resources.empty() ? "final_composition_source" : state.graph.resources.front().name;
                pass.outputs["color"] = pass.id + ".color";
                auto& raw = pass.params.raw();
                raw["name"] = pass.id;
                raw["library"] = "project";
                raw["vertex"] = "fullscreen_triangle.vert";
                raw["fragment"] = "tonemapping.frag";
                raw["pushConstants"] = true;
                raw["exposure"] = 1.0f;
                raw["method"] = 0;
                raw["publish"] = state.graph.resources.empty() ? "final_composition_source" : state.graph.resources.front().name;
                state.graph.passes.push_back(std::move(pass));
                state.markDirty();
                state.applyPositions = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Pipeline Feature"))
        {
            if (ImGui::MenuItem("compatibility_basecolor"))
            {
                state.pipelineFeatures.push_back("compatibility_basecolor");
                state.status = "Pipeline feature added in editor view. Save SRP asset support is pending.";
                state.markDirty();
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::MenuItem("general_gaussian_splat"))
            {
                state.pipelineFeatures.push_back("general_gaussian_splat");
                state.status = "Pipeline feature added in editor view. Save SRP asset support is pending.";
                state.markDirty();
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::MenuItem("final_composition"))
            {
                state.pipelineFeatures.push_back("final_composition");
                state.status = "Pipeline feature added in editor view. Save SRP asset support is pending.";
                state.markDirty();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndMenu();
        }

        ImGui::EndPopup();
    }

    void RenderGraphWindow::drawGraphEditorCanvas(EditorContext& ctx)
    {
        auto& state = *m_GraphEditor;
        state.pins.clear();
        ImNodes::EditorContextSet(state.editorContext);

        ImGui::BeginChild("##RenderGraphNodeEditor", ImVec2(0, 0), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImNodes::BeginNodeEditor();

        size_t pipelineIndex = 0;
        for (const auto& feature : state.pipelineFeatures)
        {
            const std::string nodeKey = "feature:" + feature;
            const int         id = state.nodeId("feature", feature);
            const bool        isCustomGraph = state.isCurrentGraphFeature(feature);
            pushNodeTitlePalette(isCustomGraph ? IM_COL32(145, 96, 205, 255) : IM_COL32(70, 130, 190, 255));
            ImNodes::BeginNode(id);
            ImNodes::BeginNodeTitleBar();
            drawNodeTitleText(isCustomGraph ? "Custom Render Graph" : feature.c_str());
            ImNodes::EndNodeTitleBar();
            ImGui::TextDisabled("%s", isCustomGraph ? feature.c_str() : "builtin feature region");

            ImNodes::BeginInputAttribute(state.pinId(nodeKey, "in", true), ImNodesPinShape_TriangleFilled);
            ImGui::TextDisabled("in");
            ImNodes::EndInputAttribute();
            if (isCustomGraph)
            {
                ImGui::Separator();
                for (const auto& resource : state.graph.resources)
                    ImGui::BulletText("resource: %s", resource.name.c_str());
                for (const auto& pass : state.graph.passes)
                    ImGui::BulletText("pass: %s", pass.id.c_str());
            }
            ImNodes::BeginOutputAttribute(state.pinId(nodeKey, "out", false), ImNodesPinShape_TriangleFilled);
            ImGui::Indent(170.0f);
            ImGui::TextDisabled("out");
            ImNodes::EndOutputAttribute();
            ImNodes::EndNode();
            popNodeTitlePalette();

            if (state.applyPositions)
                ImNodes::SetNodeGridSpacePos(id, ImVec2 {static_cast<float>(pipelineIndex) * 320.0f, -220.0f});
            ++pipelineIndex;
        }

        for (const auto& resource : state.graph.resources)
        {
            const int id = state.nodeId("resource_in", resource.name);
            const ImU32 resourceTitle = vrgNodeColorFromType(resource.name, true);
            pushNodeTitlePalette(resourceTitle);
            ImNodes::BeginNode(id);
            ImNodes::BeginNodeTitleBar();
            drawNodeTitleText((resource.name + " (input)").c_str());
            ImNodes::EndNodeTitleBar();
            const int out = state.pinId(resource.name, "out", false);
            ImNodes::BeginOutputAttribute(out, ImNodesPinShape_QuadFilled);
            ImGui::Indent(130.0f);
            ImGui::TextUnformatted("out");
            ImNodes::EndOutputAttribute();
            ImNodes::EndNode();
            popNodeTitlePalette();

            if (state.applyPositions)
            {
                if (auto pos = state.readNodePos(state.metaKeyForResource(resource.name)))
                    ImNodes::SetNodeGridSpacePos(id, *pos);
                else
                    ImNodes::SetNodeGridSpacePos(id, ImVec2 {260.0f, 80.0f});
            }

            const auto publishedNode = "published:" + resource.name;
            const int  publishedId = state.nodeId("resource_out", resource.name);
            pushNodeTitlePalette(IM_COL32(236, 174, 75, 255));
            ImNodes::BeginNode(publishedId);
            ImNodes::BeginNodeTitleBar();
            drawNodeTitleText((resource.name + " (published)").c_str());
            ImNodes::EndNodeTitleBar();
            ImNodes::BeginInputAttribute(state.pinId(publishedNode, "in", true), ImNodesPinShape_QuadFilled);
            ImGui::TextDisabled("in");
            ImNodes::EndInputAttribute();
            ImNodes::BeginOutputAttribute(state.pinId(publishedNode, "out", false), ImNodesPinShape_QuadFilled);
            ImGui::Indent(130.0f);
            ImGui::TextDisabled("out");
            ImNodes::EndOutputAttribute();
            ImNodes::EndNode();
            popNodeTitlePalette();
            if (state.applyPositions)
                ImNodes::SetNodeGridSpacePos(publishedId, ImVec2 {1160.0f, 80.0f});
        }

        size_t passIndex = 0;
        for (auto& pass : state.graph.passes)
        {
            if (!state.registry.contains(pass.type))
                continue;

            const auto& def = state.registry.get(pass.type);
            ensureSlots(pass, def);

            const int id = state.nodeId("pass", pass.id);
            const ImU32 passTitle = vrgNodeColorFromType(pass.type, false);
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

            const auto fragment = pass.params.get<std::string>("fragment", {});
            if (!fragment.empty())
                ImGui::TextDisabled("preset: %s", fragment.c_str());

            for (const auto& param : def.params)
            {
                bool paramDirty = false;
                drawParamField(ctx, pass, param, paramDirty);
                if (paramDirty)
                    state.markDirty();
            }

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
                const int pin = state.pinId(pass.id, slot, false);
                ImNodes::BeginOutputAttribute(pin, ImNodesPinShape_CircleFilled);
                ImGui::Indent(190.0f);
                ImGui::TextUnformatted(slot.c_str());
                ImNodes::EndOutputAttribute();
            }

            ImNodes::EndNode();
            popNodeTitlePalette();

            if (state.applyPositions)
            {
                if (auto pos = state.readNodePos(pass.id))
                    ImNodes::SetNodeGridSpacePos(id, *pos);
                else
                    ImNodes::SetNodeGridSpacePos(id, ImVec2 {680.0f + static_cast<float>(passIndex) * 300.0f, 80.0f});
            }
            ++passIndex;
        }

        std::string flowNode;
        std::string flowSlot;
        for (const auto& feature : state.pipelineFeatures)
        {
            const std::string nodeKey = "feature:" + feature;
            if (!flowNode.empty())
            {
                ImNodes::Link(state.flowLinkId(flowNode, nodeKey),
                              state.pinId(flowNode, flowSlot, false),
                              state.pinId(nodeKey, "in", true));
            }
            flowNode = nodeKey;
            flowSlot = "out";
        }

        for (const auto& pass : state.graph.passes)
        {
            for (const auto& [slot, ref] : pass.inputs)
            {
                auto parsed = parseResRef(ref);
                if (!parsed)
                    continue;
                if (!findPass(state.graph, parsed->node) && !hasResource(state.graph, parsed->node))
                    continue;

                const int from = state.pinId(parsed->node, parsed->slot, false);
                const int to = state.pinId(pass.id, slot, true);
                ImNodes::Link(state.linkId(ref, pass.id, slot), from, to);
            }

            if (pass.params.has("publish"))
            {
                const auto publish = pass.params.get<std::string>("publish", {});
                if (!publish.empty() && hasResource(state.graph, publish))
                {
                    const auto publishedNode = "published:" + publish;
                    std::string outSlot = "color";
                    if (state.registry.contains(pass.type))
                    {
                        const auto& outputs = state.registry.get(pass.type).outputs;
                        if (!outputs.empty())
                            outSlot = outputs.front();
                    }
                    ImNodes::Link(state.flowLinkId(pass.id, publish),
                                  state.pinId(pass.id, outSlot, false),
                                  state.pinId(publishedNode, "in", true));
                }
            }
        }

        ImNodes::MiniMap(0.2f, ImNodesMiniMapLocation_BottomRight);
        ImNodes::EndNodeEditor();
        state.applyPositions = false;

        int start = 0;
        int end = 0;
        if (ImNodes::IsLinkCreated(&start, &end))
        {
            auto fromIt = state.pins.find(start);
            auto toIt = state.pins.find(end);
            if (fromIt != state.pins.end() && toIt != state.pins.end())
            {
                auto from = fromIt->second;
                auto to = toIt->second;
                if (from.input && !to.input)
                    std::swap(from, to);

                if (!from.input && to.input)
                {
                    if (from.node.starts_with("feature:") || to.node.starts_with("feature:") ||
                        from.node.starts_with("published:") || to.node.starts_with("published:"))
                    {
                        state.status = "Only custom graph pass input links are editable.";
                    }
                    else
                    if (auto* dst = findPass(state.graph, to.node))
                    {
                        dst->inputs[to.slot] = makeResRef(from.node, from.slot);
                        state.markDirty();
                        state.status = "Updated pass input link";
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

        int hoveredNode = 0;
        int hoveredLink = 0;
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
                ImGui::OpenPopup("RenderGraphAddMenu");
            }
        }

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

            if (!handled && ImGui::MenuItem("Delete Resource"))
                state.removeEditableNode(state.contextNode);
            ImGui::EndPopup();
        }

        drawGraphEditorAddPopup(ctx);

        ImGui::EndChild();
        drawGameViewOverlay(ctx);
    }

    void RenderGraphWindow::drawGameViewOverlay(EditorContext& ctx)
    {
        if (ctx.state.gameViewVisibleLastFrame)
            return;

        const ImVec2 childMin = ImGui::GetItemRectMin();
        const ImVec2 childMax = ImGui::GetItemRectMax();
        const ImVec2 childSize {childMax.x - childMin.x, childMax.y - childMin.y};
        if (childSize.x < 220.0f || childSize.y < 160.0f)
            return;

        const float aspect = 16.0f / 9.0f;
        m_OverlayZoom = std::clamp(m_OverlayZoom, kOverlayZoomMin, kOverlayZoomMax);
        const float baseWidth = std::min(320.0f, std::max(180.0f, childSize.x * 0.22f));
        const float width = std::min(childSize.x - 32.0f, baseWidth * m_OverlayZoom);
        const float height = width / aspect;
        const uint32_t renderWidth = static_cast<uint32_t>(std::max(1.0f, width));
        const uint32_t renderHeight = static_cast<uint32_t>(std::max(1.0f, height));
        ensureOverlayRenderTarget(ctx, renderWidth, renderHeight);

        vultra::rhi::Texture* renderTarget =
            m_OverlayPendingRenderTarget.texture ? &*m_OverlayPendingRenderTarget.texture :
            m_OverlayActiveRenderTarget.texture  ? &*m_OverlayActiveRenderTarget.texture :
                                                    nullptr;

        bool hasPrimaryCamera = false;
        if (ctx.services && renderTarget)
        {
            if (auto* worldService = ctx.services->tryGet<vultra::IWorldService>())
            {
                auto& world = worldService->world();
                auto  camera = findPrimaryCamera(world);
                hasPrimaryCamera = camera != entt::null;
                if (hasPrimaryCamera)
                {
                    if (auto* cameraService = ctx.services->tryGet<vultra::ICameraService>())
                        cameraService->addManualCamera(makeOverlayGameCamera(world, camera, aspect, renderTarget));
                }
            }
        }

        const ImVec2 padding {14.0f, 14.0f};
        constexpr float controlHeight = 30.0f;
        const ImVec2 panelSize {width + padding.x * 2.0f, height + padding.y * 2.0f + 22.0f + controlHeight};
        const ImVec2 panelMin {childMin.x + 16.0f, childMin.y + childSize.y - panelSize.y - 16.0f};
        const ImVec2 panelMax {panelMin.x + panelSize.x, panelMin.y + panelSize.y};
        const ImVec2 imageMin {panelMin.x + padding.x, panelMin.y + padding.y + 22.0f};
        const ImVec2 imageMax {imageMin.x + width, imageMin.y + height};
        const ImVec2 controlsMin {imageMin.x, imageMax.y + 8.0f};
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const auto contains = [&](const ImVec2& min, const ImVec2& max)
        {
            return mouse.x >= min.x && mouse.x <= max.x && mouse.y >= min.y && mouse.y <= max.y;
        };
        const ImVec2 minusMin {controlsMin.x, controlsMin.y};
        const ImVec2 minusMax {minusMin.x + 24.0f, minusMin.y + 24.0f};
        const ImVec2 labelMin {minusMax.x + 10.0f, controlsMin.y + 4.0f};
        const ImVec2 plusMin {labelMin.x + 56.0f, controlsMin.y};
        const ImVec2 plusMax {plusMin.x + 24.0f, plusMin.y + 24.0f};

        const bool minusHovered = contains(minusMin, minusMax);
        const bool plusHovered  = contains(plusMin, plusMax);
        if ((minusHovered || plusHovered) && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            if (minusHovered)
                m_OverlayZoom = std::clamp(m_OverlayZoom - kOverlayZoomStep, kOverlayZoomMin, kOverlayZoomMax);
            else
                m_OverlayZoom = std::clamp(m_OverlayZoom + kOverlayZoomStep, kOverlayZoomMin, kOverlayZoomMax);
            ImGui::SetNextFrameWantCaptureMouse(true);
        }

        // ImNodes owns the editor child draw channels, so the preview is drawn in the foreground
        // and clipped back to the graph canvas. Hit testing stays manual to avoid child-window focus conflicts.
        ImDrawList* drawList = ImGui::GetForegroundDrawList(ImGui::GetWindowViewport());
        drawList->PushClipRect(childMin, childMax, true);
        drawList->AddRectFilled(panelMin, panelMax, IM_COL32(10, 14, 18, 255), 7.0f);
        drawList->AddRect(panelMin, panelMax, IM_COL32(68, 86, 105, 255), 7.0f);
        drawList->AddText(ImVec2(panelMin.x + padding.x, panelMin.y + 8.0f),
                          IM_COL32(190, 204, 218, 255),
                          "Game View");
        char zoomLabel[16] {};
        std::snprintf(zoomLabel, sizeof(zoomLabel), "%.0f%%", m_OverlayZoom * 100.0f);
        const ImVec2 zoomSize = ImGui::CalcTextSize(zoomLabel);
        drawList->AddText(ImVec2(panelMax.x - padding.x - zoomSize.x, panelMin.y + 8.0f),
                          IM_COL32(126, 142, 158, 255),
                          zoomLabel);

        if (m_OverlayActiveRenderTarget.textureId && hasPrimaryCamera)
        {
            drawList->AddRectFilled(imageMin, imageMax, IM_COL32(0, 0, 0, 255), 3.0f);
            drawList->AddImage(m_OverlayActiveRenderTarget.textureId,
                               imageMin,
                               imageMax,
                               ImVec2(0.0f, 0.0f),
                               ImVec2(1.0f, 1.0f));
        }
        else
        {
            drawList->AddRectFilled(imageMin, imageMax, IM_COL32(16, 19, 24, 255), 3.0f);
            const char* label = hasPrimaryCamera ? "Preparing preview" : "No primary camera";
            const ImVec2 textSize = ImGui::CalcTextSize(label);
            drawList->AddText(ImVec2((imageMin.x + imageMax.x - textSize.x) * 0.5f,
                                     (imageMin.y + imageMax.y - textSize.y) * 0.5f),
                              IM_COL32(140, 152, 166, 255),
                              label);
        }
        drawList->AddRect(imageMin, imageMax, IM_COL32(72, 86, 104, 255), 3.0f);
        const auto buttonColor = [](bool hovered)
        {
            return hovered ? IM_COL32(42, 50, 62, 255) : IM_COL32(26, 31, 39, 255);
        };
        drawList->AddRectFilled(minusMin, minusMax, buttonColor(minusHovered), 5.0f);
        drawList->AddText(ImVec2(minusMin.x + 4.0f, minusMin.y + 4.0f),
                          IM_COL32(184, 198, 214, 255),
                          ICON_MDI_MAGNIFY_MINUS);
        drawList->AddText(labelMin, IM_COL32(126, 142, 158, 255), zoomLabel);
        drawList->AddRectFilled(plusMin, plusMax, buttonColor(plusHovered), 5.0f);
        drawList->AddText(ImVec2(plusMin.x + 4.0f, plusMin.y + 4.0f),
                          IM_COL32(184, 198, 214, 255),
                          ICON_MDI_MAGNIFY_PLUS);
        drawList->PopClipRect();
    }

    void RenderGraphWindow::ensureOverlayRenderTarget(EditorContext& ctx, const uint32_t width, const uint32_t height)
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
            currentTarget.textureId)
            return;

        if (m_OverlayPendingRenderTarget.texture)
            retireOverlayRenderTarget(m_OverlayPendingRenderTarget);

        auto* backendService = ctx.services->tryGet<vultra::IRenderBackendService>();
        auto* imguiService   = ctx.services->tryGet<vultra::IImGuiService>();
        if (!backendService || !imguiService)
            return;

        auto& rd = backendService->renderDevice();
        auto  format = backendService->backbuffer().getPixelFormat();
        if (format == vultra::rhi::PixelFormat::eUndefined)
            format = vultra::rhi::PixelFormat::eRGBA8_UNorm;

        m_OverlayPendingRenderTarget.extent = {width, height};
        m_OverlayPendingRenderTarget.texture =
            vultra::rhi::Texture::Builder {}
                .setExtent(m_OverlayPendingRenderTarget.extent)
                .setPixelFormat(format)
                .setUsageFlags(vultra::rhi::ImageUsage::eRenderTarget | vultra::rhi::ImageUsage::eSampled)
                .build(rd);
        m_OverlayPendingRenderTarget.textureId = imguiService->addTexture(*m_OverlayPendingRenderTarget.texture);
        m_OverlayPendingRenderTarget.frameCreated = static_cast<uint64_t>(ImGui::GetFrameCount());
        m_OverlayPendingRenderTarget.releaseFrame = 0;
    }

    void RenderGraphWindow::promotePendingOverlayRenderTarget(EditorContext& ctx)
    {
        (void)ctx;
        if (!m_OverlayPendingRenderTarget.texture)
            return;

        retireOverlayRenderTarget(m_OverlayActiveRenderTarget);
        m_OverlayActiveRenderTarget = std::move(m_OverlayPendingRenderTarget);
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
        const auto frame = static_cast<uint64_t>(ImGui::GetFrameCount());
        auto* imguiService = ctx.services ? ctx.services->tryGet<vultra::IImGuiService>() : nullptr;

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
        m_OverlayActiveRenderTarget = {};
        m_OverlayPendingRenderTarget = {};
        m_OverlayRetiredRenderTargets.clear();
    }

    void RenderGraphWindow::resetOverlayRenderTargetForProject(EditorContext& ctx)
    {
        (void)ctx;
        if (m_ProjectGeneration == ctx.state.projectGeneration)
            return;

        retireOverlayRenderTarget(m_OverlayActiveRenderTarget);
        retireOverlayRenderTarget(m_OverlayPendingRenderTarget);
        m_ProjectGeneration = ctx.state.projectGeneration;
    }
} // namespace vultra_app
