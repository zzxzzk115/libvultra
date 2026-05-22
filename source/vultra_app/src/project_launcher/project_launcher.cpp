#include "project_launcher/project_launcher.hpp"

#include "vproject.hpp"

#include <vultra/core/base/common_context.hpp>
#include <vultra/core/services/window_service.hpp>

#include <IconsMaterialDesignIcons.h>
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

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

        void drawLauncherLogo(ImDrawList* drawList, ImVec2 center)
        {
            drawList->AddCircleFilled(center, 24.0f, IM_COL32(6, 10, 15, 255), 48);
            drawList->AddCircle(center, 24.0f, IM_COL32(54, 150, 220, 230), 48, 1.7f);
            drawList->AddText(ImVec2(center.x - 7.0f, center.y - 11.0f), IM_COL32(220, 236, 250, 255), "V");
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
                                  destructive ? ImVec4 {0.570f, 0.120f, 0.120f, 1.0f} :
                                                ImVec4 {0.135f, 0.165f, 0.200f, 1.0f});
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                                  destructive ? ImVec4 {0.720f, 0.140f, 0.140f, 1.0f} :
                                                ImVec4 {0.075f, 0.220f, 0.390f, 1.0f});
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

            const bool maximized = windowService->window().isMaximized();
            if (windowControlButton(maximized ? ICON_MDI_WINDOW_RESTORE : ICON_MDI_WINDOW_MAXIMIZE,
                                    maximized ? "Restore" : "Maximize"))
            {
                if (maximized)
                    windowService->window().restore();
                else
                    windowService->window().maximize();
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
                const ImU32 fill = active ? IM_COL32(33, 48, 68, 230) : IM_COL32(25, 34, 46, 210);
                drawList->AddRectFilled(min, max, fill, 7.0f);
            }

            const ImU32 iconColor = active ? IM_COL32(80, 170, 250, 255) : IM_COL32(176, 187, 200, 255);
            const ImU32 textColor = active ? IM_COL32(216, 232, 246, 255) : IM_COL32(188, 196, 207, 255);
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
                engine.ctx().config.render.renderPipelineAsset = project->renderPipeline;
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
        VULTRA_CLIENT_INFO("[Vultra] No resources.vpk found. Project Launcher mode is active.");
    }

    void ProjectLauncher::draw(AppState& state, IWindowService* windowService)
    {
        if (!m_HasScannedProjects)
            loadKnownProjects(state);

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(viewport->WorkSize, ImGuiCond_Always);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.050f, 0.063f, 0.080f, 1.0f));
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
                                IM_COL32(8, 12, 17, 255));
        drawList->AddRectFilled(origin,
                                ImVec2(origin.x + sidebarW, origin.y + size.y),
                                IM_COL32(13, 19, 27, 255));
        drawList->AddLine(ImVec2(origin.x + sidebarW, origin.y + 24.0f),
                          ImVec2(origin.x + sidebarW, origin.y + size.y - 24.0f),
                          IM_COL32(40, 50, 64, 190),
                          1.0f);

        drawLauncherLogo(drawList, ImVec2(origin.x + 64.0f, origin.y + 78.0f));
        drawList->AddText(ImVec2(origin.x + 104.0f, origin.y + 58.0f), IM_COL32(236, 241, 247, 255), "Vultra");
        drawList->AddText(ImVec2(origin.x + 104.0f, origin.y + 82.0f), IM_COL32(150, 162, 176, 255), "Project Launcher");
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
            state.mode                = AppMode::Editor;
            state.statusMessage       = "Opened a blank editor session.";
        }
        const float contentX = origin.x + sidebarW + 40.0f;
        const float contentW = std::max(420.0f, size.x - sidebarW - 80.0f);

        drawList->AddText(ImVec2(contentX, origin.y + 52.0f), IM_COL32(238, 242, 248, 255), "Projects");
        drawList->AddText(ImVec2(contentX, origin.y + 80.0f),
                          IM_COL32(146, 158, 172, 255),
                          "Choose a workspace or create a new one.");

        ImGui::SetCursorScreenPos(ImVec2(contentX, origin.y + 122.0f));
        ImGui::SetNextItemWidth(std::min(460.0f, contentW - 320.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(38.0f, 10.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.055f, 0.070f, 0.090f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.075f, 0.095f, 0.122f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.075f, 0.100f, 0.130f, 1.0f));
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
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.105f, 0.130f, 0.165f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.150f, 0.195f, 0.250f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.100f, 0.210f, 0.360f, 1.0f));
        ImGui::SetCursorScreenPos(ImVec2(origin.x + size.x - 338.0f, buttonY));
        if (ImGui::Button(ICON_MDI_FOLDER_PLUS_OUTLINE "  Add Existing", ImVec2(142.0f, 42.0f)))
            ImGui::OpenPopup("Add Existing Vultra Project");
        ImGui::SetCursorScreenPos(ImVec2(origin.x + size.x - 184.0f, buttonY));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.085f, 0.310f, 0.560f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.115f, 0.390f, 0.690f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.065f, 0.275f, 0.500f, 1.0f));
        if (ImGui::Button(ICON_MDI_PLUS "  New Project", ImVec2(144.0f, 42.0f)))
            ImGui::OpenPopup("Create Vultra Project");
        ImGui::PopStyleColor(3);
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(2);

        drawList->AddLine(ImVec2(contentX, origin.y + 178.0f),
                          ImVec2(origin.x + size.x - 40.0f, origin.y + 178.0f),
                          IM_COL32(38, 48, 61, 190),
                          1.0f);
        drawList->AddText(ImVec2(contentX, origin.y + 206.0f), IM_COL32(224, 230, 238, 255), "Recent Projects");

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

            const ImU32 rowFill = selected ? IM_COL32(31, 45, 63, 245) :
                                  hovered  ? IM_COL32(24, 33, 45, 235) :
                                             IM_COL32(17, 24, 33, 220);
            const ImU32 rowBorder = selected ? IM_COL32(70, 145, 215, 210) : IM_COL32(42, 52, 66, 170);
            drawList->AddRectFilled(rowMin, rowMax, rowFill, 7.0f);
            drawList->AddRect(rowMin, rowMax, rowBorder, 7.0f);

            const ImVec2 tileMin(rowMin.x + 14.0f, rowMin.y + 12.0f);
            const ImVec2 tileMax(tileMin.x + 58.0f, tileMin.y + 58.0f);
            drawList->AddRectFilled(tileMin, tileMax, IM_COL32(8, 13, 20, 255), 6.0f);
            drawList->AddRect(tileMin, tileMax, IM_COL32(53, 93, 128, 190), 6.0f);
            drawList->AddText(ImVec2(tileMin.x + 19.0f, tileMin.y + 17.0f), IM_COL32(102, 180, 245, 255), "V");

            drawList->PushClipRect(ImVec2(rowMin.x + 90.0f, rowMin.y),
                                   ImVec2(rowMax.x - 190.0f, rowMax.y),
                                   true);
            drawList->AddText(ImVec2(rowMin.x + 92.0f, rowMin.y + 20.0f),
                              IM_COL32(238, 242, 248, 255),
                              project.name.c_str());
            drawList->AddText(ImVec2(rowMin.x + 92.0f, rowMin.y + 46.0f),
                              IM_COL32(154, 164, 177, 255),
                              project.path.generic_string().c_str());
            drawList->PopClipRect();

            drawList->AddText(ImVec2(rowMax.x - 166.0f, rowMin.y + 22.0f),
                              IM_COL32(185, 196, 208, 255),
                              ".vproject");
            drawList->AddText(ImVec2(rowMax.x - 166.0f, rowMin.y + 48.0f),
                              IM_COL32(130, 142, 156, 255),
                              "Workspace");

            rowY += rowH + rowGap;
        }

        if (visibleCount == 0)
        {
            drawList->AddRectFilled(ImVec2(contentX, origin.y + 244.0f),
                                    ImVec2(origin.x + size.x - 40.0f, origin.y + 338.0f),
                                    IM_COL32(17, 24, 33, 180),
                                    7.0f);
            drawList->AddRect(ImVec2(contentX, origin.y + 244.0f),
                              ImVec2(origin.x + size.x - 40.0f, origin.y + 338.0f),
                              IM_COL32(42, 52, 66, 150),
                              7.0f);
            drawList->AddText(ImVec2(contentX + 24.0f, origin.y + 274.0f),
                              IM_COL32(216, 224, 233, 255),
                              "No projects found");
            drawList->AddText(ImVec2(contentX + 24.0f, origin.y + 300.0f),
                              IM_COL32(142, 154, 168, 255),
                              "Create a project or add an existing workspace.");
        }

        const bool hasSelection = m_SelectedProject >= 0 && m_SelectedProject < static_cast<int>(m_Projects.size());

        drawList->AddLine(ImVec2(contentX, origin.y + size.y - 72.0f),
                          ImVec2(origin.x + size.x - 40.0f, origin.y + size.y - 72.0f),
                          IM_COL32(38, 48, 61, 190),
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
                              IM_COL32(145, 158, 172, 255),
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
        if (!ImGui::BeginPopupModal("Create Vultra Project", &open, ImGuiWindowFlags_AlwaysAutoResize))
            return;

        ImGui::TextUnformatted("Create a .vproject in the selected project root.");
        ImGui::Separator();
        ImGui::InputText("Project Name", m_NewProjectName.data(), m_NewProjectName.size());
        m_ProjectRootDialog.draw("Project Root", m_NewProjectRoot.data(), m_NewProjectRoot.size());

        ImGui::Separator();
        if (ImGui::Button("Create"))
        {
            createProject(state);
            if (!m_NewProjectName[0])
                ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }

    void ProjectLauncher::drawAddExistingProjectPopup(AppState& state)
    {
        bool open = true;
        if (!ImGui::BeginPopupModal("Add Existing Vultra Project", &open, ImGuiWindowFlags_AlwaysAutoResize))
            return;

        ImGui::TextUnformatted("Select a project root containing a .vproject file.");
        ImGui::Separator();
        m_ExistingProjectDialog.draw("Project Root", m_ExistingProjectRoot.data(), m_ExistingProjectRoot.size());

        ImGui::Separator();
        if (ImGui::Button("Add"))
        {
            addExistingProject(state);
            if (state.statusMessage.rfind("Added existing project:", 0) == 0)
                ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
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

    void ProjectLauncher::createProject(AppState& state)
    {
        namespace fs = std::filesystem;

        const std::string projectName = sanitizeProjectName(m_NewProjectName.data());
        if (projectName.empty())
        {
            state.statusMessage = "Project name is empty.";
            return;
        }

        const fs::path projectDir = fs::path(m_NewProjectRoot.data()).lexically_normal();
        if (projectDir.empty())
        {
            state.statusMessage = "Project root is empty.";
            return;
        }

        std::error_code ec;
        const fs::path projectFile = vprojectFileFor(projectDir, projectName);
        if (fs::exists(projectFile, ec))
        {
            state.statusMessage = "Project file already exists: " + projectFile.generic_string();
            return;
        }

        fs::create_directories(projectDir / "resources" / "scenes", ec);
        if (ec)
        {
            state.statusMessage = "Failed to create project: " + ec.message();
            return;
        }

        std::string errorMessage;
        VProject    project {
               .projectDir   = projectDir,
               .name         = projectName,
               .assetRoot    = "resources",
               .defaultScene = "res://scenes/test.vscn",
        };
        if (!saveVProject(project, &errorMessage))
        {
            state.statusMessage = "Failed to write .vproject: " + errorMessage;
            return;
        }

        m_NewProjectName[0] = '\0';
        addKnownProject(state, projectDir);
        saveKnownProjects(state);
        state.statusMessage = "Created project: " + projectDir.generic_string();
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
        state.mode                = AppMode::Editor;
        state.statusMessage       = "Opened project: " + state.currentProject.generic_string();
    }
} // namespace vultra_app
