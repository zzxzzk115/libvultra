#pragma once

#include "app_state.hpp"
#include "common/file_dialog.hpp"
#include "editor_app/editor_context.hpp"
#include "editor_app/ui/editor_window_manager.hpp"
#include "launch_options.hpp"

#include <array>
#include <vultra/core/engine/engine.hpp>
#include <vultra/function/scene/vscn_document.hpp>
#include <vtask/scheduler.hpp>
#include <vtask/task_set.hpp>

#include <atomic>
#include <future>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
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
        void shutdown(EditorContext& ctx);

    private:
        enum class LoadingPhase
        {
            Idle,
            Pending,
            ImportAssets,
            ConfigureAssets,
            LoadScene,
            Finalize,
            Complete,
        };

        struct ImportTaskProgress
        {
            std::mutex mutex;
            float       progress {0.0f};
            std::string message;
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
            float                progress {0.0f};
            std::string          message;
            bool                 releasedEditorState {false};
        };

        void ensureInitialized();
        bool isProjectLoading() const;
        bool updateProjectLoading(EditorContext& ctx);
        void updateBuildAndRun(EditorContext& ctx);
        void startBuildAndRun(EditorContext& ctx);
        void beginBuildAndRun(EditorContext& ctx, const std::filesystem::path& outputFolder);
        void startProjectLoading(const std::filesystem::path& projectRoot);
        void startAssetImportTask(const std::filesystem::path& projectRoot, const std::string& assetRoot);
        void waitForAssetImportTask();
        void applySplashWindow(EditorContext& ctx);
        void applyEditorWindow(EditorContext& ctx);
        void drawLoadingOverlay() const;
        void drawBuildRunConfigurePopup(EditorContext& ctx);
        void drawBuildRunPopup();
        void drawEditorSettingsPopup(EditorContext& ctx);
        void saveCurrentScene(EditorContext& ctx);
        void syncPlaybackState(EditorContext& ctx);
        void capturePlayModeSnapshot(EditorContext& ctx);
        void restorePlayModeSnapshot(EditorContext& ctx);
        void beginDockSpace();
        void endDockSpace();
        void buildDefaultDockLayout();
        void resetDefaultDockLayout();

        EditorWindowManager m_WindowManager;
        std::filesystem::path m_SyncedProject;
        uint64_t             m_SyncedProjectGeneration {std::numeric_limits<uint64_t>::max()};
        LoadingState        m_Loading;
        std::unique_ptr<vtask::Scheduler>   m_ImportScheduler;
        std::unique_ptr<vtask::TaskSet>     m_ImportTask;
        ImportTaskResult                    m_ImportResult;
        std::atomic_bool                    m_ImportTaskDone {false};
        std::shared_ptr<ImportTaskProgress> m_ImportProgress;
        std::future<BuildRunResult>         m_BuildRunFuture;
        std::shared_ptr<BuildRunTaskProgress> m_BuildRunProgress;
        std::optional<BuildRunResult>       m_BuildRunCompleted;
        ui::FileDialogField                 m_BuildRunOutputDialog {
            "BuildRunOutputFolder",
            "Select Build Output Folder",
            ui::FileDialogMode::Directory,
        };
        std::array<char, 512>               m_BuildRunOutputFolder {};
        std::optional<vultra::SceneDocument> m_PlayModeSnapshot;
        bool                m_Initialized {false};
        bool                m_DefaultLayoutBuilt {false};
        bool                m_BuildRunActive {false};
        bool                m_BuildRunConfigureOpen {false};
        bool                m_ShowAboutPopup {false};
        bool                m_PlaybackWasPlaying {false};
        bool                m_SplashWindowApplied {false};
        bool                m_EditorWindowApplied {false};
    };
} // namespace vultra_app
