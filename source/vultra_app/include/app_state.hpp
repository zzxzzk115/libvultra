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
            float     fovY {60.0f};
        };

        struct SceneCameraAlignRequest
        {
            bool      pending {false};
            glm::vec3 position {0.0f, 6.5f, 6.5f};
            glm::quat rotation {1.0f, 0.0f, 0.0f, 0.0f};
            float     fovY {60.0f};
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
            OpenPrefab,
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
            std::string language {}; // BCP-47 UI locale (e.g. "en", "zh-CN"); empty = auto-detect from OS on first launch
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
            std::string agentCliPath; // path to the agent CLI (e.g. claude); empty = resolve "claude" on PATH
            bool        allowAgentEngineOperations {true};
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
        // Non-empty when the active scene-hierarchy document is a prefab (.vprefab) opened for
        // editing. Mirrors currentDefaultScene (same uri) so the existing scene/world plumbing keeps
        // working; only the save target semantics and labelling differ.
        std::string           currentEditingPrefab;
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
        // Per-platform export presets (mirrors VProject::exportSettings). buildSettings is the active
        // view of exportSettings[buildSettings.targetPlatform]. The active platform is transient (not
        // persisted); switching it loads that platform's preset. Keyed by platform id, then setting key.
        std::unordered_map<std::string, std::unordered_map<std::string, std::string>> exportSettings;
        // Scene document state. Tool windows should mutate this, but only document tabs should display it.
        bool                  sceneDirty {false};
        SceneCameraState      sceneCamera;
        SceneCameraAlignRequest sceneCameraAlignRequest;
        ScenePickingState     scenePicking;
    };

    // Default architecture for a target platform when no preset is saved yet.
    inline std::string defaultExportArch(const std::string& platform)
    {
        if (platform == "Android")
            return "arm64";
        if (platform == "WebGPU" || platform == "Web")
            return "wasm32";
        return "x64";
    }

    // Load a platform's saved export preset into the active BuildSettings (per-platform defaults when
    // the project has no preset for it yet). Sets the active target platform. Leaves projectName (which
    // is project-wide, not per-platform) untouched.
    inline void loadExportPreset(AppState& state, const std::string& platform)
    {
        auto& bs          = state.buildSettings;
        bs.targetPlatform = platform;

        const std::unordered_map<std::string, std::string>  empty;
        const auto                                          it = state.exportSettings.find(platform);
        const std::unordered_map<std::string, std::string>& p  = it == state.exportSettings.end() ? empty : it->second;

        const auto getOr  = [&](const char* key, const std::string& def) {
            const auto found = p.find(key);
            return found == p.end() ? def : found->second;
        };
        const auto getBool = [&](const char* key, const bool def) {
            const auto found = p.find(key);
            if (found == p.end())
                return def;
            return found->second == "true" || found->second == "1";
        };

        bs.architecture                   = getOr("architecture", defaultExportArch(platform));
        bs.configuration                  = getOr("configuration", "Development");
        bs.exportTemplatePath             = getOr("template", "");
        bs.outputDirectory                = getOr("output", "");
        bs.includeDebugSymbols            = getBool("debug_symbols", true);
        bs.compressContent                = getBool("compress_content", true);
        bs.usePakFiles                    = getBool("use_vpk", true);
        bs.additionalCommandLineArguments = getOr("extra_args", "");
    }

    // Capture the active BuildSettings into the per-platform map under the current target platform.
    inline void storeExportPreset(AppState& state)
    {
        const auto& bs       = state.buildSettings;
        auto&       p        = state.exportSettings[bs.targetPlatform];
        p["architecture"]    = bs.architecture;
        p["configuration"]   = bs.configuration;
        p["template"]        = bs.exportTemplatePath;
        p["output"]          = bs.outputDirectory;
        p["debug_symbols"]   = bs.includeDebugSymbols ? "true" : "false";
        p["compress_content"] = bs.compressContent ? "true" : "false";
        p["use_vpk"]         = bs.usePakFiles ? "true" : "false";
        p["extra_args"]      = bs.additionalCommandLineArguments;
    }
} // namespace vultra_app
