#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace vultra_app
{
    inline constexpr const char* kVPackageManifestPath = "vultra.package.vmanifest";
    inline constexpr const char* kVPackageManifestUri  = "res://vultra.package.vmanifest";

    struct VBuildScene
    {
        uint32_t    index {0};
        std::string uri;
        std::string name;
        bool        enabled {true};
    };

    struct VProject
    {
        std::filesystem::path projectDir;
        std::string           name;
        std::string           assetRoot {"resources"};
        std::string           defaultScene;
        std::vector<VBuildScene> buildScenes;
        std::string           editingRenderGraph {"res://render/default.vrg.json"};
    };

    struct VPackageManifest
    {
        std::string name;
        std::string entryScene;
        std::vector<VBuildScene> buildScenes;
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
