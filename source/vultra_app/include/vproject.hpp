#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vultra_app
{
    inline constexpr const char* kVPackageManifestPath = "vultra.package.vmanifest";
    inline constexpr const char* kVPackageManifestUri  = "res://vultra.package.vmanifest";

    struct VBuildScene
    {
        uint32_t    index {0};
        std::string uri;
        // Optional, player-defined alias. The canonical scene name is the uri filename stem
        // (see buildSceneName) and is not user-editable; alias is an additional handle.
        std::string alias;
        bool        enabled {true};
    };

    // Canonical, locked name of a build scene: the uri filename without directory or extension
    // (e.g. "res://scenes/Level_01.vscn" -> "Level_01"). Not user-editable.
    [[nodiscard]] std::string buildSceneName(std::string_view uri);

    // Resolve a build scene by either its canonical filename name or its alias (case-sensitive).
    // This is the entry point scene management should use so both handles stay interchangeable.
    [[nodiscard]] const VBuildScene* findBuildScene(const std::vector<VBuildScene>& scenes,
                                                    std::string_view                nameOrAlias);

    struct VProject
    {
        std::filesystem::path projectDir;
        std::string           name;
        std::string           assetRoot {"resources"};
        std::string           defaultScene;
        std::vector<VBuildScene> buildScenes;
        std::string           editingRenderGraph {"res://render/default.vrg.json"};
        // Ids of plugins enabled for this project (plugins are off by default). Plugins live under
        // <asset-root>/plugins; see the Plugins tab in Project Settings.
        std::vector<std::string> enabledPlugins;
    };

    struct VPackageManifest
    {
        std::string name;
        std::string entryScene;
        std::vector<VBuildScene> buildScenes;
        // res:// directory uris of plugins bundled into the package (e.g. "res://plugins/hello").
        // Only the project's enabled plugins are packed; the runtime loads each of these.
        std::vector<std::string> pluginDirs;
    };

    [[nodiscard]] std::vector<VBuildScene> normalizedBuildScenes(const std::string&              defaultScene,
                                                                 const std::vector<VBuildScene>& scenes);

    [[nodiscard]] std::filesystem::path vprojectFileFor(const std::filesystem::path& projectDir,
                                                        const std::string&           projectName);
    [[nodiscard]] std::optional<VProject> loadVProject(const std::filesystem::path& path);
    [[nodiscard]] bool                    saveVProject(const VProject& project, std::string* errorMessage = nullptr);
    [[nodiscard]] bool                    saveVPackageManifest(const std::filesystem::path& assetRoot,
                                                               const VPackageManifest&      manifest,
                                                               std::string* errorMessage = nullptr);
    [[nodiscard]] std::optional<VPackageManifest> loadVPackageManifestText(const std::string& text);
    [[nodiscard]] std::optional<VPackageManifest>
    loadVPackageManifestFromVpk(const std::filesystem::path& vpkPath, std::string* errorMessage = nullptr);
} // namespace vultra_app
