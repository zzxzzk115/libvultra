#pragma once

#include "app_state.hpp"
#include "common/asset_preview_cache.hpp"
#include "common/file_dialog.hpp"
#include "editor_app/editor_context.hpp"
#include "editor_app/examples_repository.hpp"
#include "editor_app/templates_repository.hpp"
#include "launch_options.hpp"

#include <vultra/core/engine/engine.hpp>

#include <array>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace vbase
{
    class ServiceRegistry;
}

class IWindowService;

namespace vultra_app
{
    class ProjectLauncher
    {
    public:
        static constexpr const char* kWindowTitle = "VultraEngine Project Launcher";

        static void configureAssets(vultra::Engine& engine, const LaunchOptions& options);
        static void logStartup();

        // services is the editor-shell service registry (render device + ImGui service); it powers
        // thumbnail rendering. It may be null on the fallback path, in which case grids fall back to
        // glyph icons.
        void draw(AppState& state, IWindowService* windowService, vbase::ServiceRegistry* services);

    private:
        // Every launcher flow is a page on a breadcrumb stack (no modal popups). The stack front is
        // the navigation root (Projects or Samples); deeper pages are pushed onto it.
        enum class LauncherPage
        {
            Projects,
            NewProject,
            OpenExisting,
            Samples,
            ForkExample,
        };

        struct ProjectEntry
        {
            std::filesystem::path path;
            std::string           name;
        };

        // --- Navigation ---------------------------------------------------------------------------
        LauncherPage navRoot() const { return m_PageStack.front(); }
        LauncherPage currentPage() const { return m_PageStack.back(); }
        void         pushPage(LauncherPage page);
        void         popTo(std::size_t index);
        void         resetTo(LauncherPage root);
        void         drawBreadcrumb(float x, float y);

        // --- Pages --------------------------------------------------------------------------------
        void drawProjectsPage(AppState& state, IWindowService* windowService);
        void drawNewProjectPage(AppState& state, EditorContext& ctx);
        void drawOpenExistingPage(AppState& state);
        void drawSamplesPage(AppState& state, EditorContext& ctx);
        void drawForkExamplePage(AppState& state, EditorContext& ctx);
        void refreshSamples();
        void refreshTemplates();

        // --- Project store ------------------------------------------------------------------------
        void loadKnownProjects(AppState& state);
        void saveKnownProjects(const AppState& state) const;
        void addKnownProject(AppState& state, const std::filesystem::path& path);
        bool createProject(AppState& state);
        void addExistingProject(AppState& state);
        void removeSelectedProject(AppState& state);
        void openSelectedProject(AppState& state, IWindowService* windowService);

        bool                  m_HasScannedProjects {false};
        int                   m_SelectedProject {-1};
        // Selected remote template: index into m_Templates, or -1 when none is selected/available.
        int                   m_SelectedTemplateIndex {-1};
        std::array<char, 128> m_SearchQuery {};
        std::array<char, 128> m_NewProjectName {"My Vultra Project"};
        // Set once the user edits the project folder by hand, which stops auto-deriving it from the
        // project name.
        bool                  m_NewProjectPathEdited {false};
        std::array<char, 260> m_NewProjectRoot {};
        std::array<char, 260> m_ExistingProjectRoot {};
        std::array<char, 260> m_ForkDestRoot {};
        ui::FileDialogField   m_ProjectRootDialog {
              "VultraCreateProjectRoot",
              "Choose Project Root",
              ui::FileDialogMode::Directory,
        };
        ui::FileDialogField m_ExistingProjectDialog {
            "VultraAddExistingProject",
            "Choose Existing Project Root",
            ui::FileDialogMode::Directory,
        };
        ui::FileDialogField m_ForkDestDialog {
            "VultraForkExampleDest",
            "Choose Destination Folder",
            ui::FileDialogMode::Directory,
        };
        std::vector<ProjectEntry> m_Projects;

        std::vector<LauncherPage> m_PageStack {LauncherPage::Projects};

        // --- Samples center -----------------------------------------------------------------------
        ui::AssetPreviewCache               m_Previews;
        bool                                m_SamplesFetched {false};
        std::string                         m_SamplesStatus;
        std::vector<examples::ExampleEntry> m_Examples;
        int                                 m_SelectedExample {-1};
        // Resolved (downloaded/located) thumbnail path per example id; empty path = tried and failed.
        std::unordered_map<std::string, std::filesystem::path> m_ExampleThumbs;

        // --- New Project remote templates -------------------------------------------------------
        bool                                 m_TemplatesFetched {false};
        std::string                          m_TemplatesStatus;
        std::vector<templates::TemplateEntry> m_Templates;
        std::unordered_map<std::string, std::filesystem::path> m_TemplateThumbs;
    };
} // namespace vultra_app
