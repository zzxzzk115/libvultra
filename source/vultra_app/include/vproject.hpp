#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace vultra_app
{
    struct VProject
    {
        std::filesystem::path projectDir;
        std::string           name;
        std::string           assetRoot {"resources"};
        std::string           defaultScene {"res://scenes/test.vscn"};
        std::string           renderPipeline {"res://render/default.vsrp.lua"};
    };

    [[nodiscard]] std::filesystem::path vprojectFileFor(const std::filesystem::path& projectDir,
                                                        const std::string&           projectName);
    [[nodiscard]] std::optional<VProject> loadVProject(const std::filesystem::path& path);
    [[nodiscard]] bool                    saveVProject(const VProject& project, std::string* errorMessage = nullptr);
} // namespace vultra_app
