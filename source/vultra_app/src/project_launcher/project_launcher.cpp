#include "project_launcher/project_launcher.hpp"

#include "common/ui_widgets.hpp"
#include "vproject.hpp"

#include <vultra/core/base/common_context.hpp>
#include <vultra/core/services/window_service.hpp>
#include <vultra/function/imgui/imgui_theme.hpp>

#include <IconsMaterialDesignIcons.h>
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

namespace vultra_app
{
    namespace
    {
        std::string sanitizeProjectName(std::string name)
        {
            name.erase(std::remove_if(name.begin(),
                                      name.end(),
                                      [](unsigned char ch)
                                      {
                                          return ch < 32 || ch == '<' || ch == '>' || ch == ':' || ch == '"' ||
                                                 ch == '/' || ch == '\\' || ch == '|' || ch == '?' || ch == '*';
                                      }),
                       name.end());

            while (!name.empty() && std::isspace(static_cast<unsigned char>(name.front())))
                name.erase(name.begin());
            while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back())))
                name.pop_back();

            return name;
        }

        std::filesystem::path normalizeProjectPath(const std::filesystem::path& path)
        {
            if (path.extension() == ".vproject")
                return path.parent_path().lexically_normal();
            return path.lexically_normal();
        }

        bool sameProjectPath(const std::filesystem::path& a, const std::filesystem::path& b)
        {
            return a.lexically_normal().generic_string() == b.lexically_normal().generic_string();
        }

        std::string toLower(std::string text)
        {
            std::transform(text.begin(),
                           text.end(),
                           text.begin(),
                           [](unsigned char ch)
                           {
                               return static_cast<char>(std::tolower(ch));
                           });
            return text;
        }

        bool projectMatchesSearch(const std::string& query, const std::string& name, const std::filesystem::path& path)
        {
            if (query.empty())
                return true;

            return toLower(name).find(query) != std::string::npos ||
                   toLower(path.generic_string()).find(query) != std::string::npos;
        }

        bool writeTextFile(const std::filesystem::path& path, std::string_view text, std::string& errorMessage)
        {
            namespace fs = std::filesystem;

            std::error_code ec;
            fs::create_directories(path.parent_path(), ec);
            if (ec)
            {
                errorMessage = "failed to create directory '" + path.parent_path().generic_string() + "': " + ec.message();
                return false;
            }

            std::ofstream file(path, std::ios::trunc);
            if (!file)
            {
                errorMessage = "failed to open '" + path.generic_string() + "' for writing";
                return false;
            }

            file << text;
            if (!file)
            {
                errorMessage = "failed to write '" + path.generic_string() + "'";
                return false;
            }
            return true;
        }

        bool writeDefaultProjectAssets(const std::filesystem::path& projectDir, std::string& errorMessage)
        {
            constexpr std::string_view kSampleScene = R"([vscn]
version = 1
root    = 0

[node id=1 name="Sun" parent=0 uuid="206733c1f880193cbd1dd2b65e0d0ca8"]
NameComponent/name = "Sun"
EntityStatusComponent/active = true
EntityStatusComponent/visible = true
EntityStatusComponent/locked = false
EntityStatusComponent/selectable = true
TransformComponent/position = (0, 4, 0)
TransformComponent/rotation = (-0.699544, -0.111872, 0.112315, 0.696784)
TransformComponent/scale = (1, 1, 1)
LightComponent/kind = 0
LightComponent/color = (1, 0.96, 0.9)
LightComponent/intensity = 8
LightComponent/range = 100
LightComponent/radius = 0.05
LightComponent/width = 1
LightComponent/height = 1
LightComponent/innerConeDegrees = 20
LightComponent/outerConeDegrees = 30
LightComponent/castsShadow = false
LightComponent/twoSided = false

[node id=2 name="Camera" parent=0 uuid="d6348e9e870dff93209ad02615cfefbb"]
NameComponent/name = "Camera"
EntityStatusComponent/active = true
EntityStatusComponent/visible = true
EntityStatusComponent/locked = false
EntityStatusComponent/selectable = true
TransformComponent/position = (0, 1.6, 4)
TransformComponent/rotation = (-0.130526, 2.16687e-08, 2.85274e-09, 0.991445)
TransformComponent/scale = (1, 1, 1)
CameraComponent/primary = true
CameraComponent/projection = 0
CameraComponent/fovYDegrees = 60.000000
CameraComponent/orthographicHeight = 10.000000
CameraComponent/zNear = 0.100000
CameraComponent/zFar = 1000.000000
CameraComponent/clearMode = 0
CameraComponent/clearColor = (0.02, 0.025, 0.035, 1)
CameraComponent/priority = 0
CameraComponent/rendererKey = "universal"
)";

            constexpr std::string_view kDefaultRenderGraph = R"({
  "meta": {
    "editor": {
      "nodes": {
        "CompatibilityBaseColor": {
          "pos": [
            260.0,
            80.0
          ]
        },
        "Pixelate": {
          "pos": [
            620.0,
            80.0
          ]
        },
        "FinalComposition": {
          "pos": [
            980.0,
            80.0
          ]
        }
      }
    }
  },
  "passes": [
    {
      "enabled": true,
      "id": "CompatibilityBaseColor",
      "outputs": {
        "color": "CompatibilityBaseColor.color"
      },
      "type": "CompatibilityBaseColor"
    },
    {
      "enabled": true,
      "id": "Pixelate",
      "inputs": {
        "source": "CompatibilityBaseColor.color"
      },
      "outputs": {
        "color": "Pixelate.color"
      },
      "params": {
        "name": "Pixelate"
      },
      "type": "Pixelate"
    },
    {
      "enabled": true,
      "id": "FinalComposition",
      "inputs": {
        "source": "Pixelate.color"
      },
      "outputs": {
        "target": "FinalComposition.target"
      },
      "type": "FinalComposition"
    }
  ],
  "resources": []
}
)";

            constexpr std::string_view kPixelatePass = R"(return RenderGraphPass {
    type = "Pixelate",
    shader = {
        library = "project",
        vertex = "fullscreen_triangle.vert",
        fragment = "pixelate.frag",
    },
}
)";

            constexpr std::string_view kShaderLibrary = R"(return ShaderLibrary {
    name = "project",
    root = "shaders",
    shaders = {
        "fullscreen/*.vshader",
        "generated/material_graph/*.vshader",
    },
}
)";

            constexpr std::string_view kFullscreenTriangle = R"([vshader]
language = glsl
version = 460

[vert]
layout (location = 0) out vec2 v_TexCoord;

void main() {
    v_TexCoord = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(v_TexCoord * 2.0 - 1.0, 0.0, 1.0);
}
)";

            constexpr std::string_view kPixelateShader = R"([vshader]
language = glsl
version = 460

[frag]
layout (location = 0) in vec2 v_TexCoord;
layout (location = 0) out vec4 FragColor;

layout (set = 3, binding = 0) uniform sampler2D t_0;

void main() {
    const float pixelSize = 8.0;
    vec2 sourceSize = vec2(textureSize(t_0, 0));
    vec2 pixel = floor(v_TexCoord * sourceSize / pixelSize) * pixelSize + vec2(0.5 * pixelSize);
    vec2 uv = clamp(pixel / sourceSize, vec2(0.0), vec2(1.0));
    FragColor = texture(t_0, uv);
}
)";

            const auto resourcesDir = projectDir / "resources";
            return writeTextFile(resourcesDir / "scenes" / "test.vscn", kSampleScene, errorMessage) &&
                   writeTextFile(resourcesDir / "render" / "default.vrg.json", kDefaultRenderGraph, errorMessage) &&
                   writeTextFile(resourcesDir / "render" / "passes" / "pixelate.lua", kPixelatePass, errorMessage) &&
                   writeTextFile(resourcesDir / "shaders" / "project.vshaderlib.lua", kShaderLibrary, errorMessage) &&
                   writeTextFile(resourcesDir / "shaders" / "fullscreen" / "fullscreen_triangle.vert.vshader",
                                 kFullscreenTriangle,
                                 errorMessage) &&
                   writeTextFile(resourcesDir / "shaders" / "fullscreen" / "pixelate.frag.vshader",
                                 kPixelateShader,
                                 errorMessage);
        }

        void drawLauncherLogo(ImDrawList* drawList, ImVec2 center)
        {
            namespace theme = vultra::imgui_theme;
            drawList->AddCircleFilled(center, 24.0f, theme::u32(theme::backgroundDeep()), 48);
            drawList->AddCircle(center, 24.0f, theme::u32(theme::accent()), 48, 1.7f);
            drawList->AddText(ImVec2(center.x - 7.0f, center.y - 11.0f), theme::u32(theme::text()), "V");
        }

        void setTooltip(const char* text)
        {
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("%s", text);
        }

        bool windowControlButton(const char* label, const char* tooltip, bool destructive = false)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2 {9.0f, 4.0f});
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4 {0.0f, 0.0f, 0.0f, 0.0f});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                  destructive ? vultra::imgui_theme::destructiveHovered() :
                                                vultra::imgui_theme::buttonHovered());
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                                  destructive ? vultra::imgui_theme::destructiveActive() :
                                                vultra::imgui_theme::accentButton());
            const bool pressed = ImGui::Button(label);
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(2);
            setTooltip(tooltip);
            return pressed;
        }

        void drawWindowControls(IWindowService* windowService, const ImVec2 origin, const ImVec2 size)
        {
            if (!windowService || windowService->window().isDecorated())
                return;

            ImGui::SetCursorScreenPos(ImVec2 {origin.x + size.x - 116.0f, origin.y + 18.0f});
            if (windowControlButton(ICON_MDI_WINDOW_MINIMIZE, "Minimize"))
                windowService->window().minimize();
            ImGui::SameLine(0.0f, 0.0f);

            const bool maximized = windowService->window().isFullscreen() || windowService->window().isMaximized();
            if (windowControlButton(maximized ? ICON_MDI_WINDOW_RESTORE : ICON_MDI_WINDOW_MAXIMIZE,
                                    maximized ? "Restore" : "Maximize"))
            {
                if (maximized)
                {
                    if (windowService->window().isFullscreen())
                        windowService->window().setFullscreen(false);
                    else
                        windowService->window().restore();
                }
                else
                {
                    windowService->window().setFullscreen(true);
                }
            }
            ImGui::SameLine(0.0f, 0.0f);

            if (windowControlButton(ICON_MDI_CLOSE, "Close", true))
                windowService->window().close();
        }

        bool drawSidebarButton(const char* id, const char* icon, const char* label, bool active, ImVec2 size)
        {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            ImGui::InvisibleButton(id, size);

            const bool clicked = ImGui::IsItemClicked();
            const bool hovered = ImGui::IsItemHovered();
            const ImVec2 min   = ImGui::GetItemRectMin();
            const ImVec2 max   = ImGui::GetItemRectMax();

            if (active || hovered)
            {
                auto activeFill = vultra::imgui_theme::frameActive();
                activeFill.w    = 0.90f;
                auto hoverFill  = vultra::imgui_theme::frame();
                hoverFill.w     = 0.82f;
                const ImU32 fill =
                    active ? vultra::imgui_theme::u32(activeFill) : vultra::imgui_theme::u32(hoverFill);
                drawList->AddRectFilled(min, max, fill, 7.0f);
            }

            const ImU32 iconColor = vultra::imgui_theme::u32(active ? vultra::imgui_theme::accent() :
                                                                      vultra::imgui_theme::textMuted());
            const ImU32 textColor = vultra::imgui_theme::u32(active ? vultra::imgui_theme::text() :
                                                                      vultra::imgui_theme::textMuted());
            drawList->AddText(ImVec2(min.x + 18.0f, min.y + 13.0f), iconColor, icon);
            drawList->AddText(ImVec2(min.x + 48.0f, min.y + 13.0f), textColor, label);
            return clicked;
        }
    } // namespace

    void ProjectLauncher::configureAssets(vultra::Engine& engine, const LaunchOptions& options)
    {
        engine.ctx().config.asset.loadFromVPK = false;
        if (!options.projectPath.empty())
        {
            if (auto project = loadVProject(options.projectPath); project.has_value())
            {
                engine.ctx().config.asset.assetRoot =
                    (project->projectDir / project->assetRoot).lexically_normal().generic_string();
                engine.ctx().config.render.renderPipelineAsset = project->editingRenderGraph;
            }
            else
                engine.ctx().config.asset.assetRoot =
                    (std::filesystem::path(options.projectPath) / "resources").lexically_normal().generic_string();
            return;
        }

        const std::filesystem::path launcherAssetRoot {".vultra_launcher_resources"};
        std::error_code             ec;
        std::filesystem::create_directories(launcherAssetRoot, ec);
        engine.ctx().config.asset.assetRoot = launcherAssetRoot.generic_string();
    }

    void ProjectLauncher::logStartup()
    {
        VULTRA_CLIENT_INFO("[Vultra] No default VPK found. Project Launcher mode is active.");
    }

    void ProjectLauncher::draw(AppState& state, IWindowService* windowService)
    {
        namespace theme = vultra::imgui_theme;

        if (!m_HasScannedProjects)
            loadKnownProjects(state);

        if (windowService)
        {
            auto& window = windowService->window();
            if (window.getTitle() != kWindowTitle)
            {
                window.setTitle(kWindowTitle)
                    .setDecorated(false)
                    .setResizable(true)
                    .setExtent({1280, 720})
                    .centerOnScreen()
                    .setVisible(true);
            }
        }

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(viewport->WorkSize, ImGuiCond_Always);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, vultra::imgui_theme::background());
        ImGui::Begin("##VultraProjectLauncher",
                     nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking);

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 origin  = ImGui::GetWindowPos();
        const ImVec2 size    = ImGui::GetWindowSize();

        const float sidebarW = 276.0f;
        drawList->AddRectFilled(origin,
                                ImVec2(origin.x + size.x, origin.y + size.y),
                                vultra::imgui_theme::u32(vultra::imgui_theme::background()));
        drawList->AddRectFilled(origin,
                                ImVec2(origin.x + sidebarW, origin.y + size.y),
                                vultra::imgui_theme::u32(vultra::imgui_theme::backgroundDeep()));
        drawList->AddLine(ImVec2(origin.x + sidebarW, origin.y + 24.0f),
                          ImVec2(origin.x + sidebarW, origin.y + size.y - 24.0f),
                          theme::u32(theme::withAlpha(theme::border(), 190.0f / 255.0f)),
                          1.0f);

        drawLauncherLogo(drawList, ImVec2(origin.x + 64.0f, origin.y + 78.0f));
        drawList->AddText(ImVec2(origin.x + 104.0f, origin.y + 58.0f),
                          vultra::imgui_theme::u32(vultra::imgui_theme::text()),
                          "Vultra");
        drawList->AddText(ImVec2(origin.x + 104.0f, origin.y + 82.0f),
                          theme::u32(theme::textMuted()),
                          "Project Launcher");
        drawWindowControls(windowService, origin, size);

        ImGui::SetCursorScreenPos(ImVec2(origin.x + 28.0f, origin.y + 156.0f));
        drawSidebarButton("##launcher_nav_projects", ICON_MDI_FOLDER_OUTLINE, "Projects", true, ImVec2(220.0f, 48.0f));
        ImGui::SetCursorScreenPos(ImVec2(origin.x + 28.0f, origin.y + 214.0f));
        if (drawSidebarButton("##launcher_nav_new", ICON_MDI_PLUS_CIRCLE_OUTLINE, "New Project", false, ImVec2(220.0f, 48.0f)))
            ImGui::OpenPopup("Create Vultra Project");
        ImGui::SetCursorScreenPos(ImVec2(origin.x + 28.0f, origin.y + 272.0f));
        if (drawSidebarButton("##launcher_nav_open", ICON_MDI_FOLDER_OPEN_OUTLINE, "Open Existing", false, ImVec2(220.0f, 48.0f)))
            ImGui::OpenPopup("Add Existing Vultra Project");
        ImGui::SetCursorScreenPos(ImVec2(origin.x + 28.0f, origin.y + 330.0f));
        if (drawSidebarButton("##launcher_nav_blank", ICON_MDI_WINDOW_OPEN, "Blank Editor", false, ImVec2(220.0f, 48.0f)))
        {
            state.currentProject.clear();
            state.currentProjectName.clear();
            state.selectedSourceAsset.clear();
            state.currentAssetRoot    = "resources";
            state.currentDefaultScene = "res://scenes/test.vscn";
            state.currentEditingRenderGraph = "res://render/default.vrg.json";
            ++state.projectGeneration;
            state.mode                = AppMode::Editor;
            state.statusMessage       = "Opened a blank editor session.";
        }
        const float contentX = origin.x + sidebarW + 40.0f;
        const float contentW = std::max(420.0f, size.x - sidebarW - 80.0f);

        drawList->AddText(ImVec2(contentX, origin.y + 52.0f), theme::u32(theme::text()), "Projects");
        drawList->AddText(ImVec2(contentX, origin.y + 80.0f),
                          theme::u32(theme::textMuted()),
                          "Choose a workspace or create a new one.");

        ImGui::SetCursorScreenPos(ImVec2(contentX, origin.y + 122.0f));
        ImGui::SetNextItemWidth(std::min(460.0f, contentW - 320.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(38.0f, 10.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, vultra::imgui_theme::frame());
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, vultra::imgui_theme::frameHovered());
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, vultra::imgui_theme::frameActive());
        ImGui::InputTextWithHint("##project_search",
                                 ICON_MDI_MAGNIFY "  Search projects...",
                                 m_SearchQuery.data(),
                                 m_SearchQuery.size(),
                                 ImGuiInputTextFlags_AutoSelectAll);
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(2);

        const float buttonY = origin.y + 122.0f;
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(16.0f, 10.0f));
        ImGui::PushStyleColor(ImGuiCol_Button, vultra::imgui_theme::button());
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, vultra::imgui_theme::buttonHovered());
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, vultra::imgui_theme::accentButton());
        ImGui::SetCursorScreenPos(ImVec2(origin.x + size.x - 338.0f, buttonY));
        if (ImGui::Button(ICON_MDI_FOLDER_PLUS_OUTLINE "  Add Existing", ImVec2(142.0f, 42.0f)))
            ImGui::OpenPopup("Add Existing Vultra Project");
        ImGui::SetCursorScreenPos(ImVec2(origin.x + size.x - 184.0f, buttonY));
        ImGui::PushStyleColor(ImGuiCol_Button, vultra::imgui_theme::accentButton());
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, vultra::imgui_theme::accentButtonHovered());
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, vultra::imgui_theme::accentButtonActive());
        if (ImGui::Button(ICON_MDI_PLUS "  New Project", ImVec2(144.0f, 42.0f)))
            ImGui::OpenPopup("Create Vultra Project");
        ImGui::PopStyleColor(3);
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(2);

        drawList->AddLine(ImVec2(contentX, origin.y + 178.0f),
                          ImVec2(origin.x + size.x - 40.0f, origin.y + 178.0f),
                          theme::u32(theme::withAlpha(theme::border(), 190.0f / 255.0f)),
                          1.0f);
        drawList->AddText(ImVec2(contentX, origin.y + 206.0f), theme::u32(theme::text()), "Recent Projects");

        const std::string query = toLower(m_SearchQuery.data());
        float             rowY  = origin.y + 244.0f;
        int               visibleCount = 0;
        const float       rowH         = 82.0f;
        const float       rowGap       = 10.0f;
        const float       listBottom   = origin.y + size.y - 94.0f;

        for (int i = 0; i < static_cast<int>(m_Projects.size()); ++i)
        {
            const auto& project = m_Projects[static_cast<size_t>(i)];
            if (!projectMatchesSearch(query, project.name, project.path))
                continue;
            if (rowY + rowH > listBottom)
                break;

            ++visibleCount;
            const bool selected = i == m_SelectedProject;
            const ImVec2 rowMin(contentX, rowY);
            const ImVec2 rowMax(origin.x + size.x - 40.0f, rowY + rowH);

            ImGui::SetCursorScreenPos(rowMin);
            ImGui::InvisibleButton(("##project_row_" + std::to_string(i)).c_str(), ImVec2(rowMax.x - rowMin.x, rowH));
            const bool hovered = ImGui::IsItemHovered();
            if (ImGui::IsItemClicked())
                m_SelectedProject = i;
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                m_SelectedProject = i;
                openSelectedProject(state);
            }

            const ImU32 rowFill =
                selected ? theme::u32(theme::withAlpha(theme::frameActive(), 245.0f / 255.0f)) :
                hovered  ? theme::u32(theme::withAlpha(theme::frameHovered(), 235.0f / 255.0f)) :
                           theme::u32(theme::withAlpha(theme::frame(), 220.0f / 255.0f));
            const ImU32 rowBorder = selected ? theme::u32(theme::accentTransparent(210.0f / 255.0f)) :
                                               theme::u32(theme::withAlpha(theme::border(), 170.0f / 255.0f));
            drawList->AddRectFilled(rowMin, rowMax, rowFill, 7.0f);
            drawList->AddRect(rowMin, rowMax, rowBorder, 7.0f);

            const ImVec2 tileMin(rowMin.x + 14.0f, rowMin.y + 12.0f);
            const ImVec2 tileMax(tileMin.x + 58.0f, tileMin.y + 58.0f);
            drawList->AddRectFilled(tileMin, tileMax, theme::u32(theme::backgroundDeep()), 6.0f);
            drawList->AddRect(tileMin, tileMax, theme::u32(theme::accentTransparent(190.0f / 255.0f)), 6.0f);
            drawList->AddText(ImVec2(tileMin.x + 19.0f, tileMin.y + 17.0f), theme::u32(theme::accent()), "V");

            drawList->PushClipRect(ImVec2(rowMin.x + 90.0f, rowMin.y),
                                   ImVec2(rowMax.x - 190.0f, rowMax.y),
                                   true);
            drawList->AddText(ImVec2(rowMin.x + 92.0f, rowMin.y + 20.0f),
                              theme::u32(theme::text()),
                              project.name.c_str());
            drawList->AddText(ImVec2(rowMin.x + 92.0f, rowMin.y + 46.0f),
                              theme::u32(theme::textMuted()),
                              project.path.generic_string().c_str());
            drawList->PopClipRect();

            drawList->AddText(ImVec2(rowMax.x - 166.0f, rowMin.y + 22.0f),
                              theme::u32(theme::textSoft()),
                              ".vproject");
            drawList->AddText(ImVec2(rowMax.x - 166.0f, rowMin.y + 48.0f),
                              theme::u32(theme::textMuted()),
                              "Workspace");

            rowY += rowH + rowGap;
        }

        if (visibleCount == 0)
        {
            drawList->AddRectFilled(ImVec2(contentX, origin.y + 244.0f),
                                    ImVec2(origin.x + size.x - 40.0f, origin.y + 338.0f),
                                    theme::u32(theme::withAlpha(theme::frame(), 180.0f / 255.0f)),
                                    7.0f);
            drawList->AddRect(ImVec2(contentX, origin.y + 244.0f),
                              ImVec2(origin.x + size.x - 40.0f, origin.y + 338.0f),
                              theme::u32(theme::withAlpha(theme::border(), 150.0f / 255.0f)),
                              7.0f);
            drawList->AddText(ImVec2(contentX + 24.0f, origin.y + 274.0f),
                              theme::u32(theme::text()),
                              "No projects found");
            drawList->AddText(ImVec2(contentX + 24.0f, origin.y + 300.0f),
                              theme::u32(theme::textMuted()),
                              "Create a project or add an existing workspace.");
        }

        const bool hasSelection = m_SelectedProject >= 0 && m_SelectedProject < static_cast<int>(m_Projects.size());

        drawList->AddLine(ImVec2(contentX, origin.y + size.y - 72.0f),
                          ImVec2(origin.x + size.x - 40.0f, origin.y + size.y - 72.0f),
                          theme::u32(theme::withAlpha(theme::border(), 190.0f / 255.0f)),
                          1.0f);

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14.0f, 9.0f));
        ImGui::SetCursorScreenPos(ImVec2(contentX, origin.y + size.y - 52.0f));
        if (ImGui::Button(ICON_MDI_IMPORT "  Import Project", ImVec2(142.0f, 38.0f)))
            ImGui::OpenPopup("Add Existing Vultra Project");
        ImGui::SetCursorScreenPos(ImVec2(origin.x + size.x - 378.0f, origin.y + size.y - 52.0f));
        if (!hasSelection)
            ImGui::BeginDisabled();
        if (ImGui::Button(ICON_MDI_FOLDER_OPEN "  Open", ImVec2(110.0f, 38.0f)))
            openSelectedProject(state);
        ImGui::SetCursorScreenPos(ImVec2(origin.x + size.x - 256.0f, origin.y + size.y - 52.0f));
        if (ImGui::Button(ICON_MDI_CLOSE "  Remove", ImVec2(122.0f, 38.0f)))
            removeSelectedProject(state);
        if (!hasSelection)
            ImGui::EndDisabled();
        ImGui::SetCursorScreenPos(ImVec2(origin.x + size.x - 122.0f, origin.y + size.y - 52.0f));
        if (ImGui::Button(ICON_MDI_REFRESH, ImVec2(38.0f, 38.0f)))
            loadKnownProjects(state);
        ImGui::SameLine();
        ImGui::Button(ICON_MDI_VIEW_LIST, ImVec2(38.0f, 38.0f));
        ImGui::PopStyleVar(2);

        drawCreateProjectPopup(state);
        drawAddExistingProjectPopup(state);

        if (!state.statusMessage.empty())
        {
            drawList->PushClipRect(ImVec2(contentX + 156.0f, origin.y + size.y - 54.0f),
                                   ImVec2(origin.x + size.x - 396.0f, origin.y + size.y - 18.0f),
                                   true);
            drawList->AddText(ImVec2(contentX + 160.0f, origin.y + size.y - 40.0f),
                              theme::u32(theme::textMuted()),
                              state.statusMessage.c_str());
            drawList->PopClipRect();
        }

        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
    }

    void ProjectLauncher::drawCreateProjectPopup(AppState& state)
    {
        bool open = true;
        ui::ScopedPopupStyle popupStyle;
        ImGui::SetNextWindowSizeConstraints(ImVec2 {420.0f, 0.0f}, ImVec2 {620.0f, 520.0f});
        if (!ImGui::BeginPopupModal("Create Vultra Project",
                                    &open,
                                    ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
            return;

        ui::sectionTitle(ICON_MDI_FOLDER_PLUS_OUTLINE, "New Project");
        ImGui::TextColored(ImVec4 {0.62f, 0.70f, 0.80f, 1.0f},
                           "Choose an empty folder. The folder name becomes the project name.");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        m_ProjectRootDialog.draw("Project Folder", m_NewProjectRoot.data(), m_NewProjectRoot.size());

        ImGui::Spacing();
        ImGui::Separator();
        const float buttonWidth = 96.0f;
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - buttonWidth * 2.0f - ImGui::GetStyle().ItemSpacing.x -
                             ImGui::GetStyle().WindowPadding.x);
        if (ImGui::Button(ICON_MDI_PLUS "  Create", ImVec2 {buttonWidth, 0.0f}))
        {
            if (createProject(state))
                ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2 {buttonWidth, 0.0f}))
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }

    void ProjectLauncher::drawAddExistingProjectPopup(AppState& state)
    {
        bool open = true;
        ui::ScopedPopupStyle popupStyle;
        ImGui::SetNextWindowSizeConstraints(ImVec2 {420.0f, 0.0f}, ImVec2 {620.0f, 460.0f});
        if (!ImGui::BeginPopupModal("Add Existing Vultra Project",
                                    &open,
                                    ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
            return;

        ui::sectionTitle(ICON_MDI_FOLDER_OPEN, "Existing Project");
        ImGui::TextColored(ImVec4 {0.62f, 0.70f, 0.80f, 1.0f},
                           "Select a project root containing a .vproject file.");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        m_ExistingProjectDialog.draw("Project Root", m_ExistingProjectRoot.data(), m_ExistingProjectRoot.size());

        ImGui::Spacing();
        ImGui::Separator();
        const float buttonWidth = 96.0f;
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - buttonWidth * 2.0f - ImGui::GetStyle().ItemSpacing.x -
                             ImGui::GetStyle().WindowPadding.x);
        if (ImGui::Button(ICON_MDI_PLUS "  Add", ImVec2 {buttonWidth, 0.0f}))
        {
            addExistingProject(state);
            if (state.statusMessage.rfind("Added existing project:", 0) == 0)
                ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2 {buttonWidth, 0.0f}))
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }

    void ProjectLauncher::loadKnownProjects(AppState& state)
    {
        namespace fs = std::filesystem;

        m_Projects.clear();
        m_SelectedProject = -1;

        std::error_code ec;
        fs::create_directories(state.launcherStateFile.parent_path(), ec);

        std::ifstream file(state.launcherStateFile);
        std::string   line;
        while (std::getline(file, line))
        {
            const auto projectPath = normalizeProjectPath(line);
            if (projectPath.empty())
                continue;

            addKnownProject(state, projectPath);
        }

        std::sort(m_Projects.begin(),
                  m_Projects.end(),
                  [](const ProjectEntry& a, const ProjectEntry& b) { return a.name < b.name; });
        m_HasScannedProjects = true;
    }

    void ProjectLauncher::saveKnownProjects(const AppState& state) const
    {
        namespace fs = std::filesystem;

        std::error_code ec;
        fs::create_directories(state.launcherStateFile.parent_path(), ec);

        std::ofstream file(state.launcherStateFile, std::ios::trunc);
        for (const auto& project : m_Projects)
            file << project.path.generic_string() << "\n";
    }

    void ProjectLauncher::addKnownProject(AppState& state, const std::filesystem::path& path)
    {
        const auto projectPath = normalizeProjectPath(path);
        if (projectPath.empty())
            return;

        const auto exists = std::any_of(m_Projects.begin(),
                                        m_Projects.end(),
                                        [&](const ProjectEntry& entry)
                                        {
                                            return sameProjectPath(entry.path, projectPath);
                                        });
        if (exists)
            return;

        const auto project = loadVProject(projectPath);
        if (!project.has_value())
            return;

        m_Projects.push_back(ProjectEntry {.path = project->projectDir.lexically_normal(), .name = project->name});
        state.statusMessage = "Added project: " + project->projectDir.generic_string();
    }

    bool ProjectLauncher::createProject(AppState& state)
    {
        namespace fs = std::filesystem;

        const fs::path projectDir = fs::path(m_NewProjectRoot.data()).lexically_normal();
        if (projectDir.empty())
        {
            state.statusMessage = "Project folder is empty.";
            return false;
        }

        std::error_code ec;
        if (!fs::exists(projectDir, ec))
        {
            state.statusMessage = "Project folder does not exist: " + projectDir.generic_string();
            return false;
        }
        if (!fs::is_directory(projectDir, ec))
        {
            state.statusMessage = "Project path is not a folder: " + projectDir.generic_string();
            return false;
        }
        if (!fs::is_empty(projectDir, ec) || ec)
        {
            state.statusMessage =
                ec ? "Failed to inspect project folder: " + ec.message() :
                     "Project folder must be empty: " + projectDir.generic_string();
            return false;
        }

        const std::string projectName = sanitizeProjectName(projectDir.filename().generic_string());
        if (projectName.empty())
        {
            state.statusMessage = "Project folder name is not a valid project name.";
            return false;
        }

        const fs::path projectFile = vprojectFileFor(projectDir, projectName);
        if (fs::exists(projectFile, ec))
        {
            state.statusMessage = "Project file already exists: " + projectFile.generic_string();
            return false;
        }

        fs::create_directories(projectDir / "resources" / "scenes", ec);
        if (ec)
        {
            state.statusMessage = "Failed to create project: " + ec.message();
            return false;
        }

        std::string errorMessage;
        VProject    project {
               .projectDir   = projectDir,
               .name         = projectName,
               .assetRoot    = "resources",
               .defaultScene = "res://scenes/test.vscn",
               .editingRenderGraph = "res://render/default.vrg.json",
        };
        if (!saveVProject(project, &errorMessage))
        {
            state.statusMessage = "Failed to write .vproject: " + errorMessage;
            return false;
        }
        if (!writeDefaultProjectAssets(projectDir, errorMessage))
        {
            state.statusMessage = "Failed to write default project assets: " + errorMessage;
            return false;
        }

        addKnownProject(state, projectDir);
        saveKnownProjects(state);
        state.currentProject      = project.projectDir;
        state.selectedSourceAsset.clear();
        state.currentProjectName  = project.name;
        state.currentAssetRoot    = project.assetRoot;
        state.currentDefaultScene = project.defaultScene;
        state.currentEditingRenderGraph = project.editingRenderGraph;
        ++state.projectGeneration;
        state.mode                = AppMode::Editor;
        state.statusMessage       = "Created project: " + projectDir.generic_string();
        return true;
    }

    void ProjectLauncher::addExistingProject(AppState& state)
    {
        const std::filesystem::path projectPath = normalizeProjectPath(m_ExistingProjectRoot.data());
        if (!loadVProject(projectPath).has_value())
        {
            state.statusMessage = "No .vproject found at: " + projectPath.generic_string();
            return;
        }

        addKnownProject(state, projectPath);
        saveKnownProjects(state);
        state.statusMessage = "Added existing project: " + projectPath.generic_string();
    }

    void ProjectLauncher::removeSelectedProject(AppState& state)
    {
        if (m_SelectedProject < 0 || m_SelectedProject >= static_cast<int>(m_Projects.size()))
            return;

        const auto projectDir = m_Projects[static_cast<size_t>(m_SelectedProject)];

        m_Projects.erase(m_Projects.begin() + m_SelectedProject);
        m_SelectedProject = -1;
        saveKnownProjects(state);
        state.statusMessage = "Removed project from launcher: " + projectDir.path.generic_string();
    }

    void ProjectLauncher::openSelectedProject(AppState& state)
    {
        if (m_SelectedProject < 0 || m_SelectedProject >= static_cast<int>(m_Projects.size()))
            return;

        const auto project = loadVProject(m_Projects[static_cast<size_t>(m_SelectedProject)].path);
        if (!project.has_value())
        {
            state.statusMessage = "Failed to open project.vproject.";
            return;
        }

        state.currentProject      = project->projectDir;
        state.selectedSourceAsset.clear();
        state.currentProjectName  = project->name;
        state.currentAssetRoot    = project->assetRoot;
        state.currentDefaultScene = project->defaultScene;
        state.currentEditingRenderGraph = project->editingRenderGraph;
        ++state.projectGeneration;
        state.mode                = AppMode::Editor;
        state.statusMessage       = "Opened project: " + state.currentProject.generic_string();
    }
} // namespace vultra_app
