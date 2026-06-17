#pragma once

#include "app_state.hpp"
#include "common/file_dialog.hpp"
#include "editor_app/asset_thumbnail_service.hpp"
#include "editor_app/editor_context.hpp"
#include "editor_app/editor_history.hpp"
#include "editor_app/editor_plugin_manager.hpp"
#include "editor_app/project_file_watcher.hpp"
#include "editor_app/runtime_mcp_server.hpp"
#include "editor_app/selection.hpp"
#include "editor_app/ui/editor_window_manager.hpp"
#include "launch_options.hpp"

#include <nlohmann/json_fwd.hpp>

#include <array>
#include <vtask/scheduler.hpp>
#include <vtask/task_set.hpp>
#include <vultra/core/engine/engine.hpp>
#include <vultra/function/scene/vscn_document.hpp>
#include <vultra/function/services/scene_service.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <future>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace vultra_app
{
    struct BuildRunResult
    {
        bool        ok {false};
        std::string message;
    };

    struct BuildRunTaskProgress
    {
        std::mutex  mutex;
        float       progress {0.0f};
        std::string message;
    };

    // Result of the asynchronous "Download official export template" action in the Export Settings.
    struct ExportTemplateDownloadResult
    {
        bool                  ok {false};
        std::string           status;
        std::filesystem::path templatePath;
    };

    class EditorApp
    {
    public:
        static constexpr const char* kWindowTitle = "VultraEngine Editor";

        static void configureProject(vultra::Engine& engine, const LaunchOptions& options);
        static void configureProject(vultra::Engine& engine, const std::filesystem::path& projectPath);
        static void logStartup(const LaunchOptions& options);

        void           tick(EditorContext& ctx);
        void           draw(EditorContext& ctx);
        void           updateRuntimeMcp(EditorContext& ctx);
        nlohmann::json executeCommand(EditorContext& ctx, std::string_view name, const nlohmann::json& args);
        void           shutdown(EditorContext& ctx);

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
            std::mutex  mutex;
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
            LoadingPhase            phase {LoadingPhase::Idle};
            std::filesystem::path   projectRoot;
            vultra::SceneLoadHandle sceneLoad;
            float                   progress {0.0f};
            std::string             message;
            bool                    releasedEditorState {false};
        };

        void ensureInitialized();
        // Materializes Lua-registered editor panels into windows and removes
        // unregistered ones, draining IEditorExtensionService each frame.
        void syncScriptedPanels(EditorContext& ctx);
        bool isProjectLoading() const;
        bool updateProjectLoading(EditorContext& ctx);
        void updateBuildAndRun(EditorContext& ctx);
        void startBuildAndRun(EditorContext& ctx);
        void beginBuildAndRun(EditorContext& ctx, const std::filesystem::path& outputFolder, bool launchRuntime = true);
        // Writes the project's current export settings (target platform/arch/config/template/output)
        // back into its .vproject so the choice is remembered across sessions. No-op without a project.
        void persistExportSettings(EditorContext& ctx);
        void startProjectLoading(const std::filesystem::path& projectRoot);
        void startAssetImportTask(const std::filesystem::path&       projectRoot,
                                  const std::string&                 assetRoot,
                                  std::vector<std::filesystem::path> importPaths   = {},
                                  bool                               forceReimport = false);
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
        // Editor command handlers (editor_commands.cpp), dispatched by name from
        // executeCommand's handler table. Aliased commands (scene.add/update_component,
        // scene.revert/apply_override) share a handler and branch on `name`.
        nlohmann::json cmdEditorNewScene(EditorContext& ctx, std::string_view name, const nlohmann::json& args);
        nlohmann::json cmdEditorSaveScene(EditorContext& ctx, std::string_view name, const nlohmann::json& args);
        nlohmann::json cmdEditorOpenScene(EditorContext& ctx, std::string_view name, const nlohmann::json& args);
        nlohmann::json cmdEditorOpenPrefab(EditorContext& ctx, std::string_view name, const nlohmann::json& args);
        nlohmann::json cmdEditorOpenRenderGraph(EditorContext& ctx, std::string_view name, const nlohmann::json& args);
        nlohmann::json
        cmdEditorOpenMaterialGraph(EditorContext& ctx, std::string_view name, const nlohmann::json& args);
        nlohmann::json
        cmdEditorOpenAnimatorGraph(EditorContext& ctx, std::string_view name, const nlohmann::json& args);
        nlohmann::json cmdProjectCreateEmpty(EditorContext& ctx, std::string_view name, const nlohmann::json& args);
        nlohmann::json cmdSceneNew(EditorContext& ctx, std::string_view name, const nlohmann::json& args);
        nlohmann::json cmdSceneListEntityKinds(EditorContext& ctx, std::string_view name, const nlohmann::json& args);
        nlohmann::json
        cmdSceneListComponentKinds(EditorContext& ctx, std::string_view name, const nlohmann::json& args);
        nlohmann::json cmdSceneComponentMetadata(EditorContext& ctx, std::string_view name, const nlohmann::json& args);
        nlohmann::json cmdSceneAddEntity(EditorContext& ctx, std::string_view name, const nlohmann::json& args);
        nlohmann::json cmdSceneGetComponent(EditorContext& ctx, std::string_view name, const nlohmann::json& args);
        nlohmann::json cmdSceneRemoveEntity(EditorContext& ctx, std::string_view name, const nlohmann::json& args);
        nlohmann::json cmdSceneSetComponent(EditorContext& ctx, std::string_view name, const nlohmann::json& args);
        nlohmann::json cmdSceneRemoveComponent(EditorContext& ctx, std::string_view name, const nlohmann::json& args);
        nlohmann::json cmdSceneSelectEntity(EditorContext& ctx, std::string_view name, const nlohmann::json& args);
        nlohmann::json cmdSceneMoveEntity(EditorContext& ctx, std::string_view name, const nlohmann::json& args);
        nlohmann::json cmdSceneInstantiateAsset(EditorContext& ctx, std::string_view name, const nlohmann::json& args);
        nlohmann::json cmdSceneCreatePrefab(EditorContext& ctx, std::string_view name, const nlohmann::json& args);
        nlohmann::json cmdSceneUnpackPrefab(EditorContext& ctx, std::string_view name, const nlohmann::json& args);
        nlohmann::json cmdScenePrefabOverride(EditorContext& ctx, std::string_view name, const nlohmann::json& args);
        nlohmann::json cmdEditorBackToLauncher(EditorContext& ctx, std::string_view name, const nlohmann::json& args);
        nlohmann::json cmdRuntimePlayback(EditorContext& ctx, std::string_view name, const nlohmann::json& args);
        nlohmann::json cmdEditorWindow(EditorContext& ctx, std::string_view name, const nlohmann::json& args);
        nlohmann::json cmdEditorUndo(EditorContext& ctx, std::string_view name, const nlohmann::json& args);
        nlohmann::json cmdEditorRedo(EditorContext& ctx, std::string_view name, const nlohmann::json& args);
        nlohmann::json cmdEditorHistory(EditorContext& ctx, std::string_view name, const nlohmann::json& args);
        nlohmann::json cmdEditorBuildAndRun(EditorContext& ctx, std::string_view name, const nlohmann::json& args);
        bool           openSceneFromCommand(EditorContext& ctx, const std::string& sceneUri, bool isPrefab = false);
        void           drawOpenSceneConfirmPopup(EditorContext& ctx);
        void           drawProjectSettingsPopup(EditorContext& ctx);
        void           drawEditorSettingsPopup(EditorContext& ctx);
        void           drawBuildSettingsPopup(EditorContext& ctx);
        void           drawExportTemplateDownloadRow(AppState::BuildSettings& settings);
        void           saveCurrentScene(EditorContext& ctx);
        void           saveCurrentSceneThumbnail(EditorContext& ctx);
        void           updateEditorGameClock(EditorContext& ctx);
        void           syncPlaybackState(EditorContext& ctx);
        void           capturePlayModeSnapshot(EditorContext& ctx);
        void           restorePlayModeSnapshot(EditorContext& ctx);
        void           releaseEditorStateForProjectLoad(EditorContext& ctx);
        void           beginDockSpace();
        void           endDockSpace();
        void           buildDefaultDockLayout();
        void           resetDefaultDockLayout();
        // Bring the Inspector to front whenever the selection changes, so the user sees
        // the selected item's properties (AI Chat stays the default tab otherwise).
        void updateSelectionFocus(EditorContext& ctx);

        EditorWindowManager m_WindowManager;
        EditorHistory       m_History;
        // The document whose history is active (focused). Defaults to the scene history; a
        // focused graph editor claims it via EditorContext::claimedHistory. Committed at
        // the end of each frame and read at the start of the next.
        IHistory*                             m_ActiveHistory {&m_History};
        ui::AssetThumbnailService             m_ThumbnailService;
        ProjectFileWatcher                    m_FileWatcher;
        std::filesystem::path                 m_SyncedProject;
        uint64_t                              m_SyncedProjectGeneration {std::numeric_limits<uint64_t>::max()};
        LoadingState                          m_Loading;
        std::unique_ptr<vtask::Scheduler>     m_ImportScheduler;
        std::unique_ptr<vtask::TaskSet>       m_ImportTask;
        ImportTaskResult                      m_ImportResult;
        std::atomic_bool                      m_ImportTaskDone {false};
        std::shared_ptr<ImportTaskProgress>   m_ImportProgress;
        bool                                  m_BackgroundAssetImport {false};
        std::vector<std::filesystem::path>    m_BackgroundAssetImportPaths;
        std::mutex                            m_ImportedThumbnailMutex;
        std::vector<std::filesystem::path>    m_PendingImportedThumbnailPaths;
        bool                                  m_ImportProgressPopupPendingOpen {false};
        bool                                  m_BackgroundRenderThumbnailActive {false};
        std::optional<vultra::SceneDocument>  m_BackgroundThumbnailWorldSnapshot;
        std::future<BuildRunResult>           m_BuildRunFuture;
        std::shared_ptr<BuildRunTaskProgress> m_BuildRunProgress;
        std::optional<BuildRunResult>         m_BuildRunCompleted;
        ui::FileDialogField                   m_BuildRunOutputDialog {
            "BuildRunOutputFolder",
            "Select Build Output Folder",
            ui::FileDialogMode::Directory,
        };
        ui::FileDialogField m_BuildSettingsOutputDialog {
            "BuildSettingsOutputFolder",
            "Select Build Output Folder",
            ui::FileDialogMode::Directory,
        };
        ui::FileDialogField m_ProjectAssetRootDialog {
            "ProjectAssetRootFolder",
            "Select Asset Root",
            ui::FileDialogMode::Directory,
        };
        ui::FileDialogField m_ExportTemplateDialog {
            "ExportTemplateExecutable",
            "Select Export Template",
            ui::FileDialogMode::File,
        };
        ui::FileDialogField m_ExternalEditorDialog {
            "ExternalEditorExecutable",
            "Select External Editor",
            ui::FileDialogMode::File,
        };
        std::array<char, 512>                m_BuildRunOutputFolder {};
        std::array<char, 128>                m_ProjectNameBuffer {};
        std::array<char, 256>                m_ProjectAssetRootBuffer {};
        std::array<char, 256>                m_ProjectDefaultSceneBuffer {};
        std::array<char, 256>                m_ProjectEditingRenderGraphBuffer {};
        std::array<char, 512>                m_BuildOutputFolderBuffer {};
        std::array<char, 128>                m_BuildProjectNameBuffer {};
        std::array<char, 512>                m_ExportTemplateBuffer {};
        std::array<char, 512>                m_BuildExtraArgsBuffer {};
        std::array<char, 512>                m_ExternalEditorBuffer {};
        std::array<char, 128>                m_AgentMcpServerNameBuffer {};
        std::array<char, 128>                m_AgentMcpHostBuffer {};
        std::array<char, 512>                m_AgentEndpointBuffer {};
        std::array<char, 128>                m_AgentModelBuffer {};
        std::array<char, 512>                m_AgentCliPathBuffer {};
        RuntimeMcpServer                     m_RuntimeMcpServer;
        EditorPluginManager                  m_PluginManager;
        std::future<ExportTemplateDownloadResult> m_ExportTemplateDownloadFuture;
        bool                                 m_ExportTemplateDownloading {false};
        std::string                          m_ExportTemplateDownloadStatus;
        // True while the Export Settings modal is open; used to persist export settings on close.
        bool                                 m_ExportSettingsModalActive {false};
        std::optional<vultra::SceneDocument> m_PlayModeSnapshot;
        bool                                 m_PlayModeSceneDirtySnapshot {false};
        bool                                 m_Initialized {false};
        bool                                 m_DefaultLayoutBuilt {false};
        vultra::CoreUUID                     m_LastSelectionId;
        SelectionCategory                    m_LastSelectionCategory {SelectionCategory::None};
        std::filesystem::path                m_LastSelectedSourceAsset;
        bool                                 m_BuildRunActive {false};
        bool                                 m_BuildRunPopupPendingOpen {false};
        bool                                 m_BuildRunConfigureOpen {false};
        bool                                 m_ShowAboutPopup {false};
        bool                                 m_OpenSceneConfirmPopup {false};
        std::string                          m_PendingOpenSceneUri;
        bool                                 m_PendingOpenSceneIsPrefab {false};
        bool                                 m_PlaybackWasPlaying {false};
        bool                                 m_SplashWindowApplied {false};
        bool                                 m_EditorWindowApplied {false};
    };
} // namespace vultra_app
