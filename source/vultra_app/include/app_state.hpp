#pragma once

#include "vproject.hpp"

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <filesystem>
#include <string>
#include <vector>
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
            glm::vec3 position {0.0f, 6.5f, 6.5f};
            glm::quat rotation {1.0f, 0.0f, 0.0f, 0.0f};
            float     fovYDegrees {60.0f};
        };

        struct SceneCameraAlignRequest
        {
            bool      pending {false};
            glm::vec3 position {0.0f, 6.5f, 6.5f};
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

        enum class EditorCommandType
        {
            OpenScene,
            OpenRenderGraph,
            OpenMaterialGraph,
        };

        struct EditorCommand
        {
            EditorCommandType type {EditorCommandType::OpenScene};
            std::string       payload;
        };

        struct EditorSettings
        {
            float       applicationScale {1.0f};
            float       textScale {1.0f};
            std::string theme {"Dark"};
            glm::vec4   customThemeBackground {0.050f, 0.063f, 0.080f, 1.0f};
            glm::vec4   customThemePanel {0.058f, 0.072f, 0.092f, 1.0f};
            glm::vec4   customThemeText {0.86f, 0.90f, 0.95f, 1.0f};
            glm::vec4   customThemeAccent {0.32f, 0.74f, 1.00f, 1.0f};
            std::string interfaceFont {"Inter"};
            std::string monospaceFont {"JetBrains Mono"};
            int         interfaceFontSize {14};
            int         monospaceFontSize {13};
            bool        useSystemFonts {true};
            bool        showSplashOnStartup {true};
            bool        enableAnimations {true};
            std::string externalEditor;
            bool        enableAgent {false};
            bool        autoStartMcp {false};
            std::string mcpServerName {"vultra"};
            std::string mcpHost {"127.0.0.1"};
            int         mcpPort {8848};
            std::string agentEndpoint;
            std::string agentModel;
            bool        allowAgentEngineOperations {false};
            bool        allowAgentProjectOperations {true};
            bool        requireAgentConfirmation {true};
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
        std::filesystem::path editorSettingsFile {".vultra/editor_settings.json"};
        std::filesystem::path currentProject;
        std::filesystem::path selectedSourceAsset;
        std::filesystem::path codeEditorPath;
        std::string           currentProjectName;
        std::string           currentAssetRoot {"resources"};
        std::string           currentDefaultScene;
        std::vector<VBuildScene> currentBuildScenes;
        std::string           currentEditingRenderGraph {"res://render/default.vrg.json"};
        std::string           currentEditingMaterialGraph {"res://materials/default.vmatgraph.json"};
        std::string           currentEditingAnimatorGraph {"res://animation/default.vanimgraph.json"};
        std::string           renderMode {"visible"};
        std::string           statusMessage;
        bool                  editorPlaying {false};
        bool                  editorPaused {false};
        bool                  editorStepRequested {false};
        bool                  editorSteppingThisFrame {false};
        float                 editorGameTimeSeconds {0.0f};
        float                 editorGameDeltaSeconds {0.0f};
        bool                  codeEditorOpenRequested {false};
        bool                  editorShutdownRequested {false};
        bool                  sceneViewVisible {false};
        bool                  sceneViewVisibleLastFrame {false};
        std::string           sceneViewModeRequest;
        std::string           sceneViewToolRequest;
        bool                  gameViewVisible {false};
        bool                  gameViewVisibleLastFrame {false};
        bool                  gameViewRenderTargetAvailable {false};
        bool                  gameViewRenderTargetAvailableLastFrame {false};
        uint32_t              gameViewRenderWidth {1280};
        uint32_t              gameViewRenderHeight {720};
        uint32_t              gameViewLastRenderTargetWidth {1280};
        uint32_t              gameViewLastRenderTargetHeight {720};
        bool                  metricsOverlayVisible {false};
        bool                  projectSettingsOpen {false};
        bool                  editorSettingsOpen {false};
        bool                  buildSettingsOpen {false};
        bool                  profilerWindowOpenRequested {false};
        bool                  frameDebuggerWindowOpenRequested {false};
        bool                  renderGraphOpenRequested {false};
        bool                  runtimeFrameGraphViewerOpenRequested {false};
        bool                  materialGraphOpenRequested {false};
        bool                  animatorGraphOpenRequested {false};
        std::string           editorWindowFocusRequested;
        uint64_t              projectGeneration {0};
        uint64_t              assetFileGeneration {0};
        uint64_t              sceneContentGeneration {0};
        bool                  pendingAssetImportRefresh {false};
        bool                  pendingAssetImportForceReimport {false};
        std::vector<std::filesystem::path> pendingAssetImportPaths;
        std::vector<std::filesystem::path> pendingExternalAssetDrops;
        std::vector<EditorCommand> pendingEditorCommands;
        EditorSettings        editorSettings;
        BuildSettings         buildSettings;
        // Scene document state. Tool windows should mutate this, but only document tabs should display it.
        bool                  sceneDirty {false};
        SceneCameraState      sceneCamera;
        SceneCameraAlignRequest sceneCameraAlignRequest;
        ScenePickingState     scenePicking;
    };
} // namespace vultra_app
