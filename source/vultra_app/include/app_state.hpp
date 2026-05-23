#pragma once

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <filesystem>
#include <string>
#include <cstdint>

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
        struct SceneCameraState
        {
            bool      valid {false};
            glm::vec3 position {0.0f, 1.6f, 4.0f};
            glm::quat rotation {1.0f, 0.0f, 0.0f, 0.0f};
            float     fovYDegrees {60.0f};
        };

        struct SceneCameraAlignRequest
        {
            bool      pending {false};
            glm::vec3 position {0.0f, 1.6f, 4.0f};
            glm::quat rotation {1.0f, 0.0f, 0.0f, 0.0f};
            float     fovYDegrees {60.0f};
        };

        struct ScenePickingState
        {
            bool     requested {false};
            bool     readbackPending {false};
            uint32_t x {0};
            uint32_t y {0};
        };

        AppMode               mode {AppMode::Launcher};
        std::filesystem::path launcherStateFile {".vultra/launcher_projects.txt"};
        std::filesystem::path currentProject;
        std::filesystem::path selectedSourceAsset;
        std::filesystem::path codeEditorPath;
        std::string           currentProjectName;
        std::string           currentAssetRoot {"resources"};
        std::string           currentDefaultScene {"res://scenes/test.vscn"};
        std::string           currentRenderPipeline {"res://render/default.vrg.json"};
        std::string           statusMessage;
        bool                  editorPlaying {false};
        bool                  editorPaused {false};
        bool                  editorStepRequested {false};
        bool                  codeEditorOpenRequested {false};
        bool                  editorShutdownRequested {false};
        bool                  gameViewVisible {false};
        bool                  gameViewVisibleLastFrame {false};
        bool                  metricsOverlayVisible {false};
        bool                  editorSettingsOpen {false};
        bool                  profilerWindowOpenRequested {false};
        bool                  frameDebuggerWindowOpenRequested {false};
        uint64_t              projectGeneration {0};
        // Scene document state. Tool windows should mutate this, but only document tabs should display it.
        bool                  sceneDirty {false};
        SceneCameraState      sceneCamera;
        SceneCameraAlignRequest sceneCameraAlignRequest;
        ScenePickingState     scenePicking;
    };
} // namespace vultra_app
