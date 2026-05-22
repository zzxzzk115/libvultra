#pragma once

#include "app_state.hpp"
#include "common/file_dialog.hpp"
#include "launch_options.hpp"

#include <vultra/core/engine/engine.hpp>

#include <array>
#include <filesystem>
#include <string>
#include <vector>

class IWindowService;

namespace vultra_app
{
    class ProjectLauncher
    {
    public:
        static constexpr const char* kWindowTitle = "VultraEngine Project Launcher";

        static void configureAssets(vultra::Engine& engine, const LaunchOptions& options);
        static void logStartup();

        void draw(AppState& state, IWindowService* windowService);

    private:
        struct ProjectEntry
        {
            std::filesystem::path path;
            std::string           name;
        };

        void loadKnownProjects(AppState& state);
        void saveKnownProjects(const AppState& state) const;
        void addKnownProject(AppState& state, const std::filesystem::path& path);
        void drawCreateProjectPopup(AppState& state);
        void drawAddExistingProjectPopup(AppState& state);
        void createProject(AppState& state);
        void addExistingProject(AppState& state);
        void removeSelectedProject(AppState& state);
        void openSelectedProject(AppState& state);

        bool                               m_HasScannedProjects {false};
        int                                m_SelectedProject {-1};
        std::array<char, 128>              m_SearchQuery {};
        std::array<char, 128>              m_NewProjectName {};
        std::array<char, 260>              m_NewProjectRoot {"."};
        std::array<char, 260>              m_ExistingProjectRoot {};
        ui::FileDialogField                m_ProjectRootDialog {
                           "VultraCreateProjectRoot",
                           "Choose Project Root",
                           ui::FileDialogMode::Directory,
        };
        ui::FileDialogField                m_ExistingProjectDialog {
                           "VultraAddExistingProject",
                           "Choose Existing Project Root",
                           ui::FileDialogMode::Directory,
        };
        std::vector<ProjectEntry>          m_Projects;
    };
} // namespace vultra_app
