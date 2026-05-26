#include "editor_app/ui/windows/material_graph_window.hpp"

#include <vultra/core/rhi/structs/render_device_structs.hpp>
#include <vultra/function/services/asset_service.hpp>
#include <vultra/function/services/camera_service.hpp>
#include <vultra/function/services/imgui_service.hpp>
#include <vultra/function/services/render_backend_service.hpp>
#include <vultra/function/services/render_service.hpp>
#include <vultra/function/services/shader_service.hpp>
#include <vultra/function/world/components/environment_component.hpp>
#include <vultra/function/world/components/entity_status_component.hpp>
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
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
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
        constexpr uint64_t kRenderTargetReleaseDelayFrames = 4u;
        constexpr glm::vec3 kPreviewWorldUp {0.0f, 1.0f, 0.0f};

        struct Bounds
        {
            glm::vec3 min {std::numeric_limits<float>::max()};
            glm::vec3 max {std::numeric_limits<float>::lowest()};
            bool      valid {false};

            void include(const glm::vec3& p)
            {
                min = valid ? glm::min(min, p) : p;
                max = valid ? glm::max(max, p) : p;
                valid = true;
            }

            [[nodiscard]] glm::vec3 center() const { return (min + max) * 0.5f; }
            [[nodiscard]] float radius() const { return valid ? glm::length((max - min) * 0.5f) : 0.0f; }
        };

        glm::vec3 mapPreviewArcballPoint(const ImVec2& mouse, const ImVec2& min, const ImVec2& max)
        {
            const float width = std::max(1.0f, max.x - min.x);
            const float height = std::max(1.0f, max.y - min.y);
            const float diameter = std::max(1.0f, std::min(width, height));
            const float x = (2.0f * (mouse.x - (min.x + width * 0.5f))) / diameter;
            const float y = (-2.0f * (mouse.y - (min.y + height * 0.5f))) / diameter;
            const float len2 = x * x + y * y;

            if (len2 <= 1.0f)
                return glm::normalize(glm::vec3 {x, y, std::sqrt(std::max(0.0f, 1.0f - len2))});

            const float invLen = 1.0f / std::sqrt(len2);
            return glm::vec3 {x * invLen, y * invLen, 0.0f};
        }

        glm::quat arcballDelta(const glm::vec3& from, const glm::vec3& to)
        {
            const glm::vec3 axis = glm::cross(from, to);
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

        std::string uriForPath(const EditorContext& ctx, const std::filesystem::path& path)
        {
            std::error_code ec;
            const auto rel = std::filesystem::relative(path.lexically_normal(), assetRoot(ctx), ec);
            if (ec || rel.empty())
                return {};
            return "res://" + rel.generic_string();
        }

        std::vector<std::string> collectGraphs(const EditorContext& ctx)
        {
            std::vector<std::string> out;
            const auto root = assetRoot(ctx);
            if (root.empty() || !std::filesystem::exists(root))
                return out;

            for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
            {
                if (!entry.is_regular_file())
                    continue;
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

        vultra::material_graph::Node makeNode(const vultra::material_graph::NodeDescriptor& desc,
                                              std::string id,
                                              ImVec2 pos)
        {
            vultra::material_graph::Node node;
            node.typeId = desc.typeId;
            node.id = std::move(id);
            node.displayName = desc.displayName;
            node.inputs = desc.inputs;
            node.outputs = desc.outputs;
            node.params = desc.defaultParams;
            node.editor = {{"pos", {pos.x, pos.y}}};
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
            const float     startX = ImGui::GetCursorPosX();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label);
            ImGui::SameLine();
            ImGui::SetCursorPosX(startX + kPropertyLabelWidth);
        }

        bool drawJsonValue(EditorContext&                         ctx,
                           ui::TextureSelectorState&              textureSelector,
                           vultra::material_graph::ValueType      type,
                           nlohmann::json&                        value,
                           const char*                            label)
        {
            switch (type)
            {
                case vultra::material_graph::ValueType::eBool:
                {
                    bool v = value.is_boolean() ? value.get<bool>() : false;
                    drawControlLabel(label);
                    if (ImGui::Checkbox("##value", &v))
                    {
                        value = v;
                        return true;
                    }
                    return false;
                }
                case vultra::material_graph::ValueType::eInt:
                {
                    int v = value.is_number_integer() ? value.get<int>() : 0;
                    drawControlLabel(label);
                    ImGui::SetNextItemWidth(92.0f);
                    if (ImGui::InputInt("##value", &v))
                    {
                        value = v;
                        return true;
                    }
                    return false;
                }
                case vultra::material_graph::ValueType::eFloat:
                {
                    float v = jsonFloat(value, 0.0f);
                    drawControlLabel(label);
                    ImGui::SetNextItemWidth(104.0f);
                    if (ImGui::DragFloat("##value", &v, 0.01f))
                    {
                        value = v;
                        return true;
                    }
                    return false;
                }
                case vultra::material_graph::ValueType::eVec2:
                {
                    float v[2] {0.0f, 0.0f};
                    if (value.is_array())
                        for (int i = 0; i < 2 && i < static_cast<int>(value.size()); ++i)
                            v[i] = jsonFloat(value[i], 0.0f);
                    drawControlLabel(label);
                    ImGui::SetNextItemWidth(136.0f);
                    if (ImGui::DragFloat2("##value", v, 0.01f))
                    {
                        setJsonVec(value, v, 2);
                        return true;
                    }
                    return false;
                }
                case vultra::material_graph::ValueType::eVec3:
                {
                    float v[3] {0.0f, 0.0f, 0.0f};
                    if (value.is_array())
                        for (int i = 0; i < 3 && i < static_cast<int>(value.size()); ++i)
                            v[i] = jsonFloat(value[i], 0.0f);
                    drawControlLabel(label);
                    ImGui::SetNextItemWidth(164.0f);
                    if (ImGui::DragFloat3("##value", v, 0.01f))
                    {
                        setJsonVec(value, v, 3);
                        return true;
                    }
                    return false;
                }
                case vultra::material_graph::ValueType::eColor:
                case vultra::material_graph::ValueType::eVec4:
                {
                    float v[4] {1.0f, 1.0f, 1.0f, 1.0f};
                    if (value.is_array())
                        for (int i = 0; i < 4 && i < static_cast<int>(value.size()); ++i)
                            v[i] = jsonFloat(value[i], 1.0f);
                    drawControlLabel(label);
                    ImGui::SetNextItemWidth(type == vultra::material_graph::ValueType::eColor ? 92.0f : 188.0f);
                    const bool changed = type == vultra::material_graph::ValueType::eColor ?
                                             ImGui::ColorEdit4("##value", v, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar) :
                                             ImGui::DragFloat4("##value", v, 0.01f);
                    if (changed)
                    {
                        setJsonVec(value, v, 4);
                        return true;
                    }
                    return false;
                }
                case vultra::material_graph::ValueType::eTexture2D:
                {
                    std::string uri = value.is_string() ? value.get<std::string>() : std::string {};
                    const auto textureLabel = uri.empty() ? std::string {"<none>"} : std::filesystem::path(uri).filename().generic_string();
                    drawControlLabel(label);
                    ImGui::SetNextItemWidth(148.0f);
                    if (ImGui::Button(textureLabel.c_str(), ImVec2(148.0f, 0.0f)))
                        ImGui::OpenPopup("TextureSelectorPopup");
                    ui::TextureSelection selection;
                    if (ui::drawTextureSelectorPopup(ctx, "TextureSelectorPopup", textureSelector, uri, &selection))
                    {
                        value = selection.uri;
                        return true;
                    }
                    return false;
                }
                case vultra::material_graph::ValueType::eString:
                {
                    std::array<char, 256> buffer {};
                    if (value.is_string())
                        std::snprintf(buffer.data(), buffer.size(), "%s", value.get<std::string>().c_str());
                    drawControlLabel(label);
                    ImGui::SetNextItemWidth(180.0f);
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
            std::string current = value.is_string() ? value.get<std::string>() : std::string(options.empty() ? "" : options.front());
            bool changed = false;
            drawControlLabel(label);
            ImGui::SetNextItemWidth(156.0f);
            if (ImGui::BeginCombo("##value", current.c_str()))
            {
                for (const char* option : options)
                {
                    const bool selected = current == option;
                    if (ImGui::Selectable(option, selected))
                    {
                        current = option;
                        value = current;
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
            if (node.typeId != "vultra.output.surface")
                return false;

            if (key == "shadingModel")
            {
                static constexpr std::array options {
                    "PBR_MR",
                    "PBR_SpecGloss",
                    "Phong",
                    "Unlit",
                    "ToonLike",
                };
                return drawStringCombo(value, key.c_str(), options);
            }

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
                                                              const std::string& key)
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

        std::string_view nodeMenuSubcategory(std::string_view typeId)
        {
            if (typeId.starts_with("vultra.input."))
                return "Vertex Attributes";
            if (typeId == "vultra.param.texture2d")
                return "Textures";
            return {};
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

            const auto mesh = assets->loadMeshSync(meshUuid);
            if (!mesh.ready() || !mesh.cpu())
                return bounds;

            for (const auto& p : mesh.cpu()->positions)
                bounds.include(glm::vec3 {p.x, p.y, p.z});
            return bounds;
        }

        vultra::RenderCamera makePreviewCamera(const glm::vec3& position,
                                               const glm::vec3& targetPosition,
                                               const float      fovY,
                                               const float      aspect,
                                               vultra::rhi::Texture* target,
                                               const bool       useSkybox)
        {
            vultra::RenderCamera cam {};
            cam.name = "Material Graph Preview";
            cam.view = glm::lookAt(position, targetPosition, kPreviewWorldUp);
            cam.projection = glm::perspectiveRH_ZO(glm::radians(fovY), std::max(aspect, 0.0001f), 0.05f, 1000.0f);
            cam.zNear = 0.05f;
            cam.zFar = 1000.0f;
            cam.fovY = glm::radians(fovY);
            cam.target = target;
            cam.clearValue = {0.035f, 0.04f, 0.047f, 1.0f};
            cam.clearMode = useSkybox ? 1u : 0u;
            cam.renderImGui = false;
            cam.rendererKey = "universal";
            return cam;
        }
    } // namespace

    MaterialGraphWindow::MaterialGraphWindow() :
        EditorWindow("Material Graph", ICON_MDI_MOLECULE),
        m_Registry(vultra::material_graph::makeBuiltinNodeRegistry())
    {
        m_NodeEditor = ImNodes::EditorContextCreate();
    }

    MaterialGraphWindow::~MaterialGraphWindow()
    {
        if (m_NodeEditor)
            ImNodes::EditorContextFree(m_NodeEditor);
    }

    void MaterialGraphWindow::draw(EditorContext& ctx)
    {
        ensureLoaded(ctx);

        ImGuiWindowFlags flags = ImGuiWindowFlags_None;
        if (m_Dirty)
            flags |= ImGuiWindowFlags_UnsavedDocument;
        if (!ImGui::Begin(title().c_str(), &m_Open, flags))
        {
            ImGui::End();
            return;
        }

        drawToolbar(ctx);
        ImGui::Separator();

        const float rightWidth = std::clamp(ImGui::GetContentRegionAvail().x * 0.30f, 320.0f, 460.0f);
        const float leftWidth = std::max(240.0f, ImGui::GetContentRegionAvail().x - rightWidth - ImGui::GetStyle().ItemSpacing.x);
        ImGui::BeginChild("##MaterialGraphCanvas", ImVec2(leftWidth, 0.0f), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        drawNodeEditor(ctx);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##MaterialGraphSide", ImVec2(0.0f, 0.0f), true);
        drawPreview(ctx);
        ImGui::Separator();
        drawInspector(ctx);
        ImGui::EndChild();

        ImGui::End();
    }

    void MaterialGraphWindow::onDestroy(EditorContext& ctx)
    {
        if (ctx.services)
            if (auto* renderService = ctx.services->tryGet<vultra::IRenderService>())
                renderService->releaseOverrideRenderWorld(&m_PreviewWorld);
        releasePreviewRenderTarget(ctx);
    }

    void MaterialGraphWindow::ensureLoaded(EditorContext& ctx)
    {
        if (m_Loaded)
            return;
        m_Loaded = true;
        if (!loadGraph(ctx, m_CurrentUri))
            newGraph(ctx);
    }

    void MaterialGraphWindow::newGraph(EditorContext& ctx)
    {
        m_Graph = {};
        m_Graph.name = "Default Material Graph";
        m_CurrentUri = "res://materials/default.vmatgraph.json";
        const auto* color = m_Registry.find("vultra.param.color");
        const auto* output = m_Registry.find("vultra.output.surface");
        if (color && output)
        {
            m_Graph.nodes.push_back(makeNode(*color, "Color", ImVec2(-220.0f, 20.0f)));
            m_Graph.nodes.push_back(makeNode(*output, "Surface", ImVec2(160.0f, 0.0f)));
            m_Graph.links.push_back({.from = {.nodeId = "Color", .pin = "value"}, .to = {.nodeId = "Surface", .pin = "baseColor"}});
        }
        markDirty(ctx);
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
        auto graph = vultra::material_graph::loadGraphFromText(text.value(), &diagnostics);
        m_Diagnostics = std::move(diagnostics);
        if (!graph)
            return false;
        m_Graph = std::move(*graph);
        for (auto& node : m_Graph.nodes)
            ensureNodePorts(node);
        m_CurrentUri = std::move(uri);
        m_Dirty = false;
        m_Status = "Loaded";
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
            m_Status = "Save failed";
            return false;
        }
        file << vultra::material_graph::saveGraphToText(m_Graph);
        file.close();
        if (auto* assets = ctx.services ? ctx.services->tryGet<vultra::IAssetService>() : nullptr)
        {
            assets->clearTextAssetOverride(m_CurrentUri);
            assets->reimportAsset(m_CurrentUri, true);
        }
        m_Dirty = false;
        m_Status = "Saved";
        if (m_LiveApply)
            compileGraph(ctx);
        return true;
    }

    bool MaterialGraphWindow::compileGraph(EditorContext& ctx)
    {
        vultra::material_graph::MaterialGraphCompiler compiler {m_Registry};
        vultra::material_graph::SurfaceFunctionBackend backend;
        const auto shaderId = vultra::material_graph::sanitizeShaderId(std::filesystem::path(m_CurrentUri).stem().generic_string());
        auto result = compiler.compile({.graph = m_Graph, .shaderId = shaderId, .graphId = vultra::material_graph::stableGraphId(m_CurrentUri)}, backend);
        if (!result)
        {
            m_Diagnostics = result.error();
            m_Status = "Compile failed";
            return false;
        }
        m_Diagnostics = result->diagnostics;

        const auto outDir = assetRoot(ctx) / "shaders" / "generated" / "material_graph";
        std::filesystem::create_directories(outDir);
        const auto outPath = outDir / (shaderId + ".frag.vshader");
        std::ofstream file(outPath, std::ios::binary | std::ios::trunc);
        if (!file)
        {
            m_Status = "Failed to write generated shader";
            return false;
        }
        file << "[vshader]\nlanguage = glsl\nversion = 460\n\n[frag]\n";
        file << "#extension GL_EXT_nonuniform_qualifier : require\n\n";
        file << "#define VULTRA_DECLARE_BINDLESS_TEXTURES\n";
        file << "#include \"include/common/gpu_scene.glsl\"\n\n";
        file << result->vshaderSource << "\n";
        file << "layout(location = 0) out vec4 FragColor;\n";
        file << "void main()\n{\n";
        file << "    MaterialGraphSurface surface = eval_material_graph_" << vultra::material_graph::sanitizeShaderId(shaderId)
             << "(0u, vec2(0.0), vec3(0.0), vec3(0.0, 1.0, 0.0), vec3(0.0, 0.0, 1.0));\n";
        file << "    FragColor = surface.baseColor;\n";
        file << "}\n";
        file.close();

        if (auto* assets = ctx.services ? ctx.services->tryGet<vultra::IAssetService>() : nullptr)
            assets->reimportAsset("res://shaders/project.vshaderlib.lua", true);
        if (auto* shaders = ctx.services ? ctx.services->tryGet<vultra::IShaderService>() : nullptr)
            (void)shaders->reloadProjectLibrary("res://shaders/project.vshaderlib.lua");
        m_Status = "Compiled: " + outPath.generic_string();
        return true;
    }

    void MaterialGraphWindow::drawToolbar(EditorContext& ctx)
    {
        ImGui::SetNextItemWidth(330.0f);
        if (ImGui::BeginCombo("Graph", m_CurrentUri.c_str()))
        {
            for (const auto& uri : collectGraphs(ctx))
                if (ImGui::Selectable(uri.c_str(), uri == m_CurrentUri))
                    loadGraph(ctx, uri);
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_MDI_FILE_PLUS " New"))
            newGraph(ctx);
        ImGui::SameLine();
        if (ImGui::Button(ICON_MDI_CONTENT_SAVE " Save"))
            saveGraph(ctx);
        ImGui::SameLine();
        if (ImGui::Button(ICON_MDI_COG_PLAY " Compile"))
            compileGraph(ctx);
        ImGui::SameLine();
        ImGui::Checkbox("Live Apply", &m_LiveApply);
        if (!m_Status.empty())
        {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", m_Status.c_str());
        }
    }

    void MaterialGraphWindow::drawNodeEditor(EditorContext& ctx)
    {
        m_Pins.clear();
        const bool graphCanvasHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        ImNodes::EditorContextSet(m_NodeEditor);
        ImNodes::BeginNodeEditor();
        for (auto& node : m_Graph.nodes)
        {
            ensureNodePorts(node);
            const int id = nodeId(node.id);
            ImNodes::BeginNode(id);
            ImNodes::BeginNodeTitleBar();
            ImGui::TextUnformatted(node.displayName.empty() ? node.id.c_str() : node.displayName.c_str());
            ImNodes::EndNodeTitleBar();

            for (const auto& pin : node.inputs)
            {
                ImNodes::BeginInputAttribute(pinId(node.id, pin.name, true), ImNodesPinShape_CircleFilled);
                ImGui::TextUnformatted(pin.name.c_str());
                ImNodes::EndInputAttribute();
            }

            for (const auto& [key, raw] : node.params.items())
            {
                if (std::ranges::any_of(node.inputs, [&](const auto& pin) { return pin.name == key && hasInputLink(m_Graph, node.id, pin.name); }))
                    continue;
                auto* desc = m_Registry.find(node.typeId);
                vultra::material_graph::ValueType type = vultra::material_graph::ValueType::eUnknown;
                if (desc)
                    type = parameterTypeForKey(*desc, key);
                auto& value = node.params[key];
                ImGui::PushID(key.c_str());
                const bool changed =
                    drawKnownEnumParam(node, key, value) || drawJsonValue(ctx, m_TextureSelector, type, value, key.c_str());
                ImGui::PopID();
                if (changed)
                    markDirty(ctx);
            }

            for (const auto& pin : node.outputs)
            {
                ImNodes::BeginOutputAttribute(pinId(node.id, pin.name, false), ImNodesPinShape_CircleFilled);
                ImGui::TextUnformatted(pin.name.c_str());
                ImNodes::EndOutputAttribute();
            }
            ImNodes::EndNode();

            if (node.editor.contains("pos"))
            {
                const auto& pos = node.editor["pos"];
                if (pos.is_array() && pos.size() == 2)
                    ImNodes::SetNodeGridSpacePos(id, ImVec2(pos[0].get<float>(), pos[1].get<float>()));
                node.editor.erase("pos");
            }
        }

        for (const auto& link : m_Graph.links)
            ImNodes::Link(linkId(link), pinId(link.from.nodeId, link.from.pin, false), pinId(link.to.nodeId, link.to.pin, true));
        ImNodes::MiniMap(0.18f, ImNodesMiniMapLocation_BottomRight);
        ImNodes::EndNodeEditor();

        for (auto& node : m_Graph.nodes)
        {
            const auto pos = ImNodes::GetNodeGridSpacePos(nodeId(node.id));
            node.editor["pos"] = {pos.x, pos.y};
        }

        int start = 0;
        int end = 0;
        if (ImNodes::IsLinkCreated(&start, &end))
        {
            auto a = m_Pins.find(start);
            auto b = m_Pins.find(end);
            if (a != m_Pins.end() && b != m_Pins.end())
            {
                auto from = a->second;
                auto to = b->second;
                if (from.input)
                    std::swap(from, to);
                if (!from.input && to.input)
                {
                    std::erase_if(m_Graph.links, [&](const auto& link) { return link.to.nodeId == to.node && link.to.pin == to.pin; });
                    m_Graph.links.push_back({.from = {.nodeId = from.node, .pin = from.pin}, .to = {.nodeId = to.node, .pin = to.pin}});
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

        int hovered = 0;
        if (ImNodes::IsNodeHovered(&hovered) && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
        {
            m_ContextNode = hovered;
            ImGui::OpenPopup("MaterialGraphNodeMenu");
        }
        if (graphCanvasHovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right) && hovered == 0)
            ImGui::OpenPopup("MaterialGraphAddNode");
        if (ImGui::BeginPopup("MaterialGraphNodeMenu"))
        {
            if (ImGui::MenuItem("Delete"))
            {
                std::string node;
                for (const auto& item : m_Pins)
                    if (nodeId(item.second.node) == m_ContextNode)
                        node = item.second.node;
                if (!node.empty() && node != "Surface")
                {
                    std::erase_if(m_Graph.links, [&](const auto& link) { return link.from.nodeId == node || link.to.nodeId == node; });
                    std::erase_if(m_Graph.nodes, [&](const auto& n) { return n.id == node; });
                    markDirty(ctx);
                }
            }
            ImGui::EndPopup();
        }
        drawAddNodePopup(ctx);
    }

    void MaterialGraphWindow::drawInspector(EditorContext&)
    {
        ImGui::TextUnformatted("Diagnostics");
        if (m_Diagnostics.empty())
            ImGui::TextDisabled("No diagnostics.");
        for (const auto& diag : m_Diagnostics)
        {
            const ImVec4 color = diag.severity == vultra::material_graph::Diagnostic::Severity::eError ?
                                     ImVec4(1.0f, 0.35f, 0.35f, 1.0f) :
                                     ImVec4(1.0f, 0.75f, 0.25f, 1.0f);
            ImGui::TextColored(color, "%s%s%s", diag.message.c_str(), diag.nodeId.empty() ? "" : " @ ", diag.nodeId.c_str());
        }
    }

    void MaterialGraphWindow::updatePreviewFocusAnimation()
    {
        if (!m_PreviewFocusActive)
            return;

        const float dt = std::max(ImGui::GetIO().DeltaTime, 0.0f);
        m_PreviewFocusElapsed = std::min(m_PreviewFocusElapsed + dt, m_PreviewFocusDuration);
        const float t = m_PreviewFocusDuration > 0.0f ?
                            std::clamp(m_PreviewFocusElapsed / m_PreviewFocusDuration, 0.0f, 1.0f) :
                            1.0f;
        const float eased = 1.0f - std::pow(1.0f - t, 3.0f);
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
            m_PreviewArcballActive = false;
            m_PreviewDistanceScale = 1.0f;
        }

        m_PreviewBoundsCenter = bounds.center();
        const float radius = std::max(bounds.radius(), 0.25f);
        const float fovY = glm::radians(std::clamp(m_PreviewCameraFovY, 5.0f, 160.0f));
        const float safeAspect = std::max(aspect, 0.0001f);
        const float tanY = std::tan(fovY * 0.5f);
        const float tanX = tanY * safeAspect;
        const float fitDistance = radius / std::max(std::min(tanX, tanY), 0.0001f);
        m_PreviewFitDistance = std::max(fitDistance * 1.08f, radius + 0.35f);
        const glm::vec3 orbitDirection = glm::normalize(glm::vec3 {0.55f, 0.32f, 0.74f});
        const glm::vec3 targetPosition = m_PreviewBoundsCenter + orbitDirection * m_PreviewFitDistance * m_PreviewDistanceScale;

        m_PreviewFocusStartPosition = resetAngle ? targetPosition : m_PreviewCameraPosition;
        m_PreviewFocusTargetPosition = targetPosition;
        m_PreviewFocusElapsed = 0.0f;
        m_PreviewFocusDuration = resetAngle ? 0.0f : 0.28f;
        m_PreviewFocusActive = !resetAngle &&
                               glm::length(m_PreviewFocusTargetPosition - m_PreviewFocusStartPosition) > 0.0001f;
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
                mesh->mesh = m_PreviewMesh;
                mesh->builtinGeometry = m_PreviewMesh.valid() ? UINT32_MAX : 2u;
                if (mesh->materialOverrides.empty())
                    mesh->materialOverrides.push_back({});
                mesh->materialOverrides.front().slot = 0u;
                mesh->materialOverrides.front().materialGraph = m_CurrentUri;
            }
            if (auto* tr = reg.try_get<vultra::TransformComponent>(m_PreviewSphere))
            {
                tr->rotation = m_PreviewObjectRotation;
                tr->position = glm::vec3 {0.0f};
                tr->scale = glm::vec3 {1.0f};
                tr->worldMatrix = glm::translate(glm::mat4 {1.0f}, m_PreviewBoundsCenter) *
                                  glm::mat4_cast(tr->rotation) *
                                  glm::translate(glm::mat4 {1.0f}, -m_PreviewBoundsCenter);
                tr->dirty = false;
            }
            if (m_PreviewEnvironment != entt::null && reg.valid(m_PreviewEnvironment))
            {
                if (auto* environment = reg.try_get<vultra::EnvironmentComponent>(m_PreviewEnvironment))
                {
                    environment->skybox = m_PreviewSkybox;
                    environment->ambientColor = glm::vec3 {0.28f, 0.30f, 0.34f};
                    environment->ambientIntensity = 1.6f;
                    environment->enableIBL = m_PreviewSkybox.valid();
                    environment->iblColor = glm::vec3 {0.45f, 0.48f, 0.52f};
                    environment->iblIntensity = 1.2f;
                }
            }
        }
        const float size = std::min(ImGui::GetContentRegionAvail().x, 260.0f);
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
            m_PreviewCameraPosition = m_PreviewBoundsCenter + orbitDirection * m_PreviewFitDistance * m_PreviewDistanceScale;
        }

        if (m_PreviewLight != entt::null && m_PreviewWorld.registry().valid(m_PreviewLight))
        {
            if (auto* lightTransform = m_PreviewWorld.registry().try_get<vultra::TransformComponent>(m_PreviewLight))
            {
                const glm::vec3 lightDirection = glm::normalize(m_PreviewBoundsCenter - m_PreviewCameraPosition);
                lightTransform->rotation = glm::quatLookAt(lightDirection, kPreviewWorldUp);
                lightTransform->dirty = true;
            }
        }

        ensurePreviewRenderTarget(ctx, static_cast<uint32_t>(std::max(size, 32.0f)), static_cast<uint32_t>(std::max(size, 32.0f)));
        if (ctx.services && m_PreviewTarget.texture)
        {
            if (auto* cameras = ctx.services->tryGet<vultra::ICameraService>())
            {
                auto cam = makePreviewCamera(m_PreviewCameraPosition,
                                             m_PreviewBoundsCenter,
                                             m_PreviewCameraFovY,
                                             aspect,
                                             &*m_PreviewTarget.texture,
                                             m_PreviewSkybox.valid());
                cam.worldOverride = &m_PreviewWorld;
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
        const bool previewHovered = ImGui::IsItemHovered();
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
                m_PreviewObjectRotation = glm::normalize(arcballDelta(m_PreviewArcballVector, next) * m_PreviewObjectRotation);
                m_PreviewArcballVector = next;
            }

            const float wheel = ImGui::GetIO().MouseWheel;
            if (std::abs(wheel) > 0.0f)
            {
                m_PreviewFocusActive = false;
                m_PreviewDistanceScale = std::clamp(m_PreviewDistanceScale * std::exp(-wheel * 0.16f), 0.08f, 8.0f);
            }
        }
        if (ui::drawMeshUuidField(ctx, "Preview Mesh", m_PreviewMesh, m_MeshSelector))
        {
            m_Status = m_PreviewMesh.valid() ? "Preview mesh changed" : "Preview mesh reset to sphere";
            focusPreviewMesh(ctx, aspect, true);
        }
        if (ui::drawTextureUuidField(ctx, "Skybox", m_PreviewSkybox, m_TextureSelector))
            m_Status = m_PreviewSkybox.valid() ? "Preview skybox changed" : "Preview skybox cleared";
        ImGui::TextDisabled(m_PreviewMesh.valid() ? "Preview: selected mesh using slot 0 override" :
                                                    "Preview: sphere using slot 0 override");
        ImGui::TextDisabled("Left drag: rotate preview  Wheel: zoom  F: frame");
    }

    void MaterialGraphWindow::drawAddNodePopup(EditorContext& ctx)
    {
        if (!ImGui::BeginPopup("MaterialGraphAddNode"))
            return;

        const auto addNodeItem = [&](const vultra::material_graph::NodeDescriptor& desc) {
            if (!ImGui::MenuItem(desc.displayName.c_str()))
                return;

            const std::string stem = nodeIdStem(desc);
            std::string       unique = stem;
            int               suffix = 1;
            while (findNode(unique))
                unique = stem + std::to_string(++suffix);
            const ImVec2 panning = ImNodes::EditorContextGetPanning();
            const float  offset = static_cast<float>(m_Graph.nodes.size() % 6u) * 32.0f;
            m_Graph.nodes.push_back(makeNode(desc, unique, ImVec2(-panning.x + 80.0f + offset, -panning.y + 80.0f + offset)));
            markDirty(ctx);
        };

        const auto drawNodeTypeItem = [&](const std::string& typeId) {
            const auto* desc = m_Registry.find(typeId);
            if (!desc)
                return;
            if (typeId == "vultra.output.surface" && std::ranges::any_of(m_Graph.nodes, [](const auto& node) {
                    return node.typeId == "vultra.output.surface";
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
        const auto typeIds = m_Registry.typeIds();
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

            if (ImGui::BeginMenu(category))
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
                        if (ImGui::BeginMenu(subcategory))
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
        m_Dirty = true;
        if (auto* assets = ctx.services ? ctx.services->tryGet<vultra::IAssetService>() : nullptr)
            assets->setTextAssetOverride(m_CurrentUri, vultra::material_graph::saveGraphToText(m_Graph));
        m_Status = m_LiveApply ? "Live preview updated" : "Edited";
    }

    void MaterialGraphWindow::ensurePreviewWorld(EditorContext&)
    {
        if (m_PreviewSphere != entt::null)
            return;
        auto& reg = m_PreviewWorld.registry();

        m_PreviewLight = m_PreviewWorld.createEntity();
        reg.emplace<vultra::IDComponent>(m_PreviewLight);
        reg.emplace<vultra::NameComponent>(m_PreviewLight, vultra::NameComponent {"Preview Key Light"});
        reg.emplace<vultra::TransformComponent>(m_PreviewLight);
        auto& lightTransform = reg.get<vultra::TransformComponent>(m_PreviewLight);
        lightTransform.rotation = glm::quatLookAt(glm::normalize(glm::vec3(0.4f, -0.8f, 0.35f)), glm::vec3(0.0f, 1.0f, 0.0f));
        lightTransform.worldMatrix = glm::mat4_cast(lightTransform.rotation);
        reg.emplace<vultra::LightComponent>(m_PreviewLight);
        auto& l = reg.get<vultra::LightComponent>(m_PreviewLight);
        l.kind = 0u;
        l.intensity = 6.0f;
        l.castsShadow = false;

        m_PreviewEnvironment = m_PreviewWorld.createEntity();
        reg.emplace<vultra::IDComponent>(m_PreviewEnvironment);
        reg.emplace<vultra::NameComponent>(m_PreviewEnvironment, vultra::NameComponent {"Preview Environment"});
        reg.emplace<vultra::EnvironmentComponent>(m_PreviewEnvironment,
                                                  vultra::EnvironmentComponent {
                                                      .ambientColor = glm::vec3 {0.28f, 0.30f, 0.34f},
                                                      .ambientIntensity = 1.6f,
                                                      .enableIBL = false,
                                                      .iblColor = glm::vec3 {0.45f, 0.48f, 0.52f},
                                                      .iblIntensity = 1.2f,
                                                  });

        m_PreviewSphere = m_PreviewWorld.createEntity();
        reg.emplace<vultra::IDComponent>(m_PreviewSphere);
        reg.emplace<vultra::NameComponent>(m_PreviewSphere, vultra::NameComponent {"Preview Sphere"});
        reg.emplace<vultra::TransformComponent>(m_PreviewSphere);
        reg.emplace<vultra::MeshComponent>(m_PreviewSphere,
                                           vultra::MeshComponent {
                                               .builtinGeometry = 2u,
                                               .materialOverrides = {{.slot = 0u, .materialGraph = m_CurrentUri}},
                                           });
    }

    void MaterialGraphWindow::ensurePreviewRenderTarget(EditorContext& ctx, uint32_t width, uint32_t height)
    {
        if (!ctx.services || width == 0u || height == 0u)
            return;
        collectRetiredPreviewTargets(ctx);
        if (m_PreviewTarget.texture && m_PreviewTarget.extent.width == width && m_PreviewTarget.extent.height == height && m_PreviewTarget.textureId)
            return;
        releasePreviewRenderTarget(ctx);
        auto* backend = ctx.services->tryGet<vultra::IRenderBackendService>();
        auto* imgui = ctx.services->tryGet<vultra::IImGuiService>();
        if (!backend || !imgui)
            return;
        auto format = backend->backbuffer().getPixelFormat();
        if (format == vultra::rhi::PixelFormat::eUndefined)
            format = vultra::rhi::PixelFormat::eRGBA8_UNorm;
        m_PreviewTarget.extent = {width, height};
        m_PreviewTarget.texture = vultra::rhi::Texture::Builder {}
                                      .setExtent(m_PreviewTarget.extent)
                                      .setPixelFormat(format)
                                      .setNumMipLevels(1)
                                      .setUsageFlags(vultra::rhi::ImageUsage::eRenderTarget | vultra::rhi::ImageUsage::eSampled)
                                      .build(backend->renderDevice());
        m_PreviewTarget.textureId = imgui->addTexture(*m_PreviewTarget.texture);
    }

    void MaterialGraphWindow::releasePreviewRenderTarget(EditorContext& ctx)
    {
        if (ctx.services)
            if (auto* imgui = ctx.services->tryGet<vultra::IImGuiService>())
                if (m_PreviewTarget.textureId)
                    imgui->removeTexture(m_PreviewTarget.textureId);
        m_PreviewTarget = {};
    }

    void MaterialGraphWindow::collectRetiredPreviewTargets(EditorContext& ctx)
    {
        const auto frame = static_cast<uint64_t>(ImGui::GetFrameCount());
        auto* imgui = ctx.services ? ctx.services->tryGet<vultra::IImGuiService>() : nullptr;
        std::size_t out = 0;
        for (auto& slot : m_RetiredPreviewTargets)
        {
            if (frame >= slot.releaseFrame)
            {
                if (imgui && slot.textureId)
                    imgui->removeTexture(slot.textureId);
            }
            else
            {
                m_RetiredPreviewTargets[out++] = std::move(slot);
            }
        }
        m_RetiredPreviewTargets.resize(out);
    }

    int MaterialGraphWindow::nodeId(std::string_view id) const { return stableImNodesId(id); }

    int MaterialGraphWindow::pinId(std::string_view node, std::string_view pin, bool input)
    {
        const std::string key = std::string(input ? "in:" : "out:") + std::string(node) + ":" + std::string(pin);
        const int id = stableImNodesId(key);
        m_Pins[id] = {std::string(node), std::string(pin), input};
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
        node.inputs = desc->inputs;
        node.outputs = desc->outputs;
        for (const auto& [key, value] : desc->defaultParams.items())
            if (!node.params.contains(key))
                node.params[key] = value;
        for (const auto& input : desc->inputs)
            if (input.defaultValue && !node.params.contains(input.name))
                node.params[input.name] = *input.defaultValue;
    }
} // namespace vultra_app
