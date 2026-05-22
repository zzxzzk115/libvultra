#include "project_launcher/project_launcher.hpp"

#include "vproject.hpp"

#include <vultra/core/base/common_context.hpp>

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
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

    void ProjectLauncher::draw(AppState& state)
    {
        if (!m_HasScannedProjects)
            loadKnownProjects(state);

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos, ImGuiCond_Once);
        ImGui::SetNextWindowSize(viewport->WorkSize, ImGuiCond_Once);

        ImGui::Begin(kWindowTitle);
        ImGui::TextUnformatted("Choose a project, add an existing one, or create a fresh workspace.");
        ImGui::Text("Known projects: %s", state.launcherStateFile.generic_string().c_str());
        ImGui::Separator();

        if (ImGui::Button("Create Project"))
            ImGui::OpenPopup("Create Vultra Project");
        ImGui::SameLine();
        if (ImGui::Button("Add Existing"))
            ImGui::OpenPopup("Add Existing Vultra Project");
        ImGui::SameLine();
        if (ImGui::Button("Refresh"))
            loadKnownProjects(state);

        drawCreateProjectPopup(state);
        drawAddExistingProjectPopup(state);

        ImGui::SeparatorText("Projects");
        if (ImGui::BeginListBox("##ProjectList", ImVec2(-FLT_MIN, 220.0f)))
        {
            for (int i = 0; i < static_cast<int>(m_Projects.size()); ++i)
            {
                const bool  selected = i == m_SelectedProject;
                const auto& project  = m_Projects[static_cast<size_t>(i)];
                const auto  label    = project.name + "##" + project.path.generic_string();
                if (ImGui::Selectable(label.c_str(), selected))
                    m_SelectedProject = i;
            }
            ImGui::EndListBox();
        }

        const bool hasSelection = m_SelectedProject >= 0 && m_SelectedProject < static_cast<int>(m_Projects.size());
        if (!hasSelection)
            ImGui::BeginDisabled();
        if (ImGui::Button("Open Project"))
            openSelectedProject(state);
        ImGui::SameLine();
        if (ImGui::Button("Remove From List"))
            removeSelectedProject(state);
        if (!hasSelection)
            ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button("Open Blank Editor"))
        {
            state.currentProject.clear();
            state.currentProjectName.clear();
            state.selectedSourceAsset.clear();
            state.currentAssetRoot    = "resources";
            state.currentDefaultScene = "res://scenes/test.vscn";
            state.mode                = AppMode::Editor;
            state.statusMessage       = "Opened a blank editor session.";
        }

        if (!state.statusMessage.empty())
        {
            ImGui::Separator();
            ImGui::TextWrapped("%s", state.statusMessage.c_str());
        }

        ImGui::End();
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
