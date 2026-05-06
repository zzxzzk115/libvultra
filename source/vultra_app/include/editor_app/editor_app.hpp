#pragma once

#include "app_state.hpp"
#include "editor_app/editor_context.hpp"
#include "editor_app/ui/editor_window_manager.hpp"
#include "launch_options.hpp"

#include <vultra/core/engine/engine.hpp>

#include <filesystem>

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

    private:
        void ensureInitialized();
        void syncProjectRuntime(EditorContext& ctx);
        void drawMainMenuBar(EditorContext& ctx);
        void buildDefaultDockLayout();

        EditorWindowManager m_WindowManager;
        std::filesystem::path m_SyncedProject;
        bool                m_Initialized {false};
        bool                m_DefaultLayoutBuilt {false};
        bool                m_ShowAboutPopup {false};
    };
} // namespace vultra_app
