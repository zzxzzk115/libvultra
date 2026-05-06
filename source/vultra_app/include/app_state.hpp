#pragma once

#include <filesystem>
#include <string>

namespace vultra_app
{
    enum class AppMode
    {
        Runtime,
        Launcher,
        Editor,
    };

    struct AppState
    {
        AppMode               mode {AppMode::Launcher};
        std::filesystem::path launcherStateFile {".vultra/launcher_projects.txt"};
        std::filesystem::path currentProject;
        std::filesystem::path selectedSourceAsset;
        std::string           currentProjectName;
        std::string           currentAssetRoot {"resources"};
        std::string           currentDefaultScene {"res://scenes/main.vscn"};
        std::string           statusMessage;
        bool                  editorPlaying {false};
        bool                  editorPaused {false};
    };
} // namespace vultra_app
