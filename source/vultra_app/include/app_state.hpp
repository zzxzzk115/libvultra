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

        struct EditorSettings
        {
            float       applicationScale {1.0f};
            float       textScale {1.0f};
            std::string theme {"Dark"};
            std::string interfaceFont {"Inter"};
            std::string monospaceFont {"JetBrains Mono"};
            int         interfaceFontSize {14};
            int         monospaceFontSize {13};
            bool        useSystemFonts {true};
            bool        showSplashOnStartup {true};
            bool        enableAnimations {true};
            std::string externalEditor;
        };

        struct BuildSettings
        {
            std::string targetPlatform {"Windows"};
            std::string architecture {"x64"};
            std::string configuration {"Development"};
            std::string exportTemplatePath;
            std::string outputDirectory;
            std::string projectName;
            bool        includeDebugSymbols {true};
            bool        compressContent {true};
            bool        usePakFiles {true};
            std::string additionalCommandLineArguments;
            std::string lastBuildStatus {"Idle"};
            std::string lastBuildTime {"Never"};
            std::string buildLog;
        };

        AppMode               mode {AppMode::Launcher};
        std::filesystem::path launcherStateFile {".vultra/launcher_projects.txt"};
        std::filesystem::path currentProject;
        std::filesystem::path selectedSourceAsset;
        std::filesystem::path codeEditorPath;
        std::string           currentProjectName;
        std::string           currentAssetRoot {"resources"};
        std::string           currentDefaultScene {"res://scenes/test.vscn"};
        std::string           currentEditingRenderGraph {"res://render/default.vrg.json"};
        std::string           statusMessage;
        bool                  editorPlaying {false};
        bool                  editorPaused {false};
        bool                  editorStepRequested {false};
        bool                  codeEditorOpenRequested {false};
        bool                  editorShutdownRequested {false};
        bool                  gameViewVisible {false};
        bool                  gameViewVisibleLastFrame {false};
        uint32_t              gameViewRenderWidth {1280};
        uint32_t              gameViewRenderHeight {720};
        bool                  metricsOverlayVisible {false};
        bool                  projectSettingsOpen {false};
        bool                  editorSettingsOpen {false};
        bool                  buildSettingsOpen {false};
        bool                  profilerWindowOpenRequested {false};
        bool                  frameDebuggerWindowOpenRequested {false};
        uint64_t              projectGeneration {0};
        EditorSettings        editorSettings;
        BuildSettings         buildSettings;
        // Scene document state. Tool windows should mutate this, but only document tabs should display it.
        bool                  sceneDirty {false};
        SceneCameraState      sceneCamera;
        SceneCameraAlignRequest sceneCameraAlignRequest;
        ScenePickingState     scenePicking;
    };
} // namespace vultra_app
