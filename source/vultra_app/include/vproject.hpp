#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace vultra_app
{
    inline constexpr const char* kVPackageManifestPath = "vultra.package.vmanifest";
    inline constexpr const char* kVPackageManifestUri  = "res://vultra.package.vmanifest";

    struct VProject
    {
        std::filesystem::path projectDir;
        std::string           name;
        std::string           assetRoot {"resources"};
        std::string           defaultScene {"res://scenes/test.vscn"};
        std::string           editingRenderGraph {"res://render/default.vrg.json"};
    };

    struct VPackageManifest
    {
        std::string name;
        std::string entryScene {"res://scenes/test.vscn"};
    };

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
