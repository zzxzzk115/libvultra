#pragma once

#include "app_state.hpp"
#include "common/file_dialog.hpp"
#include "editor_app/asset_thumbnail_service.hpp"
#include "editor_app/editor_context.hpp"
#include "editor_app/editor_history.hpp"
#include "editor_app/project_file_watcher.hpp"
#include "editor_app/runtime_mcp_server.hpp"
#include "editor_app/ui/editor_window_manager.hpp"
#include "launch_options.hpp"

#include <nlohmann/json_fwd.hpp>

#include <array>
#include <vultra/core/engine/engine.hpp>
#include <vultra/function/scene/vscn_document.hpp>
#include <vultra/function/services/scene_service.hpp>
#include <vtask/scheduler.hpp>
#include <vtask/task_set.hpp>

#include <atomic>
#include <future>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <cstdint>
#include <limits>

namespace vultra_app
{
    struct BuildRunResult
    {
        bool        ok {false};
        std::string message;
    };

    struct BuildRunTaskProgress
    {
        std::mutex mutex;
        float       progress {0.0f};
        std::string message;
    };

    class EditorApp
    {
    public:
        static constexpr const char* kWindowTitle = "VultraEngine Editor";

        static void configureProject(vultra::Engine& engine, const LaunchOptions& options);
        static void configureProject(vultra::Engine& engine, const std::filesystem::path& projectPath);
        static void logStartup(const LaunchOptions& options);

        void tick(EditorContext& ctx);
        void draw(EditorContext& ctx);
        void updateRuntimeMcp(EditorContext& ctx);
        nlohmann::json executeCommand(EditorContext& ctx, std::string_view name, const nlohmann::json& args);
        void shutdown(EditorContext& ctx);

    private:
        enum class LoadingPhase
        {
            Idle,
            Pending,
            ShowSplash,
            ImportAssets,
            ConfigureAssets,
            GenerateThumbnails,
            LoadScene,
            Finalize,
            Complete,
        };

        struct ImportTaskProgress
        {
            std::mutex mutex;
            float       progress {0.0f};
            std::string message;
            std::string currentItem;
            size_t      processedItems {0};
            size_t      totalItems {0};
        };

        struct ImportTaskResult
        {
            bool        ok {false};
            std::string error;
            std::string assetRoot;
            std::string registryPath;
        };

        struct LoadingState
        {
            LoadingPhase         phase {LoadingPhase::Idle};
            std::filesystem::path projectRoot;
            vultra::SceneLoadHandle sceneLoad;
            float                progress {0.0f};
            std::string          message;
            bool                 releasedEditorState {false};
        };

        void ensureInitialized();
        bool isProjectLoading() const;
        bool updateProjectLoading(EditorContext& ctx);
        void updateBuildAndRun(EditorContext& ctx);
        void startBuildAndRun(EditorContext& ctx);
        void beginBuildAndRun(EditorContext& ctx, const std::filesystem::path& outputFolder, bool launchRuntime = true);
        void startProjectLoading(const std::filesystem::path& projectRoot);
        void startAssetImportTask(const std::filesystem::path&              projectRoot,
                                  const std::string&                       assetRoot,
                                  std::vector<std::filesystem::path>       importPaths = {});
        void waitForAssetImportTask();
        void updateBackgroundAssetImport(EditorContext& ctx);
        void updateBackgroundThumbnails(EditorContext& ctx);
        void reloadRuntimeAssetRegistry(EditorContext& ctx);
        void applySplashWindow(EditorContext& ctx);
        void applyEditorWindow(EditorContext& ctx);
        void drawLoadingOverlay(EditorContext& ctx) const;
        void drawEditorTaskBar(EditorContext& ctx);
        void drawImportProgressPopup();
        void drawBuildRunConfigurePopup(EditorContext& ctx);
        void drawBuildRunPopup();
        void processEditorCommands(EditorContext& ctx);
        bool openSceneFromCommand(EditorContext& ctx, const std::string& sceneUri);
        void drawOpenSceneConfirmPopup(EditorContext& ctx);
        void drawProjectSettingsPopup(EditorContext& ctx);
        void drawEditorSettingsPopup(EditorContext& ctx);
        void drawBuildSettingsPopup(EditorContext& ctx);
        void saveCurrentScene(EditorContext& ctx);
        void saveCurrentSceneThumbnail(EditorContext& ctx);
        void updateEditorGameClock(EditorContext& ctx);
        void syncPlaybackState(EditorContext& ctx);
        void capturePlayModeSnapshot(EditorContext& ctx);
        void restorePlayModeSnapshot(EditorContext& ctx);
        void releaseEditorStateForProjectLoad(EditorContext& ctx);
        void beginDockSpace();
        void endDockSpace();
        void buildDefaultDockLayout();
        void resetDefaultDockLayout();

        EditorWindowManager m_WindowManager;
        EditorHistory       m_History;
        ui::AssetThumbnailService m_ThumbnailService;
        ProjectFileWatcher m_FileWatcher;
        std::filesystem::path m_SyncedProject;
        uint64_t             m_SyncedProjectGeneration {std::numeric_limits<uint64_t>::max()};
        LoadingState        m_Loading;
        std::unique_ptr<vtask::Scheduler>   m_ImportScheduler;
        std::unique_ptr<vtask::TaskSet>     m_ImportTask;
        ImportTaskResult                    m_ImportResult;
        std::atomic_bool                    m_ImportTaskDone {false};
        std::shared_ptr<ImportTaskProgress> m_ImportProgress;
        bool                                m_BackgroundAssetImport {false};
        std::vector<std::filesystem::path>  m_BackgroundAssetImportPaths;
        std::mutex                          m_ImportedThumbnailMutex;
        std::vector<std::filesystem::path>  m_PendingImportedThumbnailPaths;
        bool                                m_ImportProgressPopupPendingOpen {false};
        bool                                m_BackgroundRenderThumbnailActive {false};
        std::optional<vultra::SceneDocument> m_BackgroundThumbnailWorldSnapshot;
        std::future<BuildRunResult>         m_BuildRunFuture;
        std::shared_ptr<BuildRunTaskProgress> m_BuildRunProgress;
        std::optional<BuildRunResult>       m_BuildRunCompleted;
        ui::FileDialogField                 m_BuildRunOutputDialog {
            "BuildRunOutputFolder",
            "Select Build Output Folder",
            ui::FileDialogMode::Directory,
        };
        ui::FileDialogField                 m_BuildSettingsOutputDialog {
            "BuildSettingsOutputFolder",
            "Select Build Output Folder",
            ui::FileDialogMode::Directory,
        };
        ui::FileDialogField                 m_ProjectAssetRootDialog {
            "ProjectAssetRootFolder",
            "Select Asset Root",
            ui::FileDialogMode::Directory,
        };
        ui::FileDialogField                 m_ExportTemplateDialog {
            "ExportTemplateExecutable",
            "Select Export Template",
            ui::FileDialogMode::File,
        };
        ui::FileDialogField                 m_ExternalEditorDialog {
            "ExternalEditorExecutable",
            "Select External Editor",
            ui::FileDialogMode::File,
        };
        std::array<char, 512>               m_BuildRunOutputFolder {};
        std::array<char, 128>               m_ProjectNameBuffer {};
        std::array<char, 256>               m_ProjectAssetRootBuffer {};
        std::array<char, 256>               m_ProjectDefaultSceneBuffer {};
        std::array<char, 256>               m_ProjectEditingRenderGraphBuffer {};
        std::array<char, 512>               m_BuildOutputFolderBuffer {};
        std::array<char, 128>               m_BuildProjectNameBuffer {};
        std::array<char, 512>               m_ExportTemplateBuffer {};
        std::array<char, 512>               m_BuildExtraArgsBuffer {};
        std::array<char, 512>               m_ExternalEditorBuffer {};
        std::array<char, 128>               m_AgentMcpServerNameBuffer {};
        std::array<char, 128>               m_AgentMcpHostBuffer {};
        std::array<char, 512>               m_AgentEndpointBuffer {};
        std::array<char, 128>               m_AgentModelBuffer {};
        RuntimeMcpServer                    m_RuntimeMcpServer;
        std::optional<vultra::SceneDocument> m_PlayModeSnapshot;
        bool                m_PlayModeSceneDirtySnapshot {false};
        bool                m_Initialized {false};
        bool                m_DefaultLayoutBuilt {false};
        bool                m_BuildRunActive {false};
        bool                m_BuildRunPopupPendingOpen {false};
        bool                m_BuildRunConfigureOpen {false};
        bool                m_ShowAboutPopup {false};
        bool                m_OpenSceneConfirmPopup {false};
        std::string         m_PendingOpenSceneUri;
        bool                m_PlaybackWasPlaying {false};
        bool                m_SplashWindowApplied {false};
        bool                m_EditorWindowApplied {false};
    };
} // namespace vultra_app
