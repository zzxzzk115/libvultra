#pragma once

#include "app_state.hpp"
#include "editor_app/editor_context.hpp"
#include "editor_app/ui/editor_window_manager.hpp"
#include "launch_options.hpp"

#include <vultra/core/engine/engine.hpp>
#include <vultra/function/scene/vscn_document.hpp>

#include <filesystem>
#include <optional>

namespace vultra_app
{
    class EditorApp
    {
    public:
        static constexpr const char* kWindowTitle = "VultraEngine Editor";

        static void configureProject(vultra::Engine& engine, const LaunchOptions& options);
        static void configureProject(vultra::Engine& engine, const std::filesystem::path& projectPath);
        static void logStartup(const LaunchOptions& options);

        void draw(EditorContext& ctx);
        void shutdown(EditorContext& ctx);

    private:
        void ensureInitialized();
        void syncProjectRuntime(EditorContext& ctx);
        void syncPlaybackState(EditorContext& ctx);
        void capturePlayModeSnapshot(EditorContext& ctx);
        void restorePlayModeSnapshot(EditorContext& ctx);
        void drawMainMenuBar(EditorContext& ctx);
        void buildDefaultDockLayout();

        EditorWindowManager m_WindowManager;
        std::filesystem::path m_SyncedProject;
        std::optional<vultra::SceneDocument> m_PlayModeSnapshot;
        bool                m_Initialized {false};
        bool                m_DefaultLayoutBuilt {false};
        bool                m_ShowAboutPopup {false};
        bool                m_PlaybackWasPlaying {false};
    };
} // namespace vultra_app
