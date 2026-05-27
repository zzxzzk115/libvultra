#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace vultra_app
{
    struct LaunchOptions
    {
        bool        showHelp {false};
        bool        editorMode {false};
        bool        cliOnly {false};
        std::string cliCommand;
        std::string projectPath;
        std::string vpkPath;
        std::string sceneUri;
        std::optional<bool> validation;
        std::optional<bool> debugMarkers;
        std::optional<bool> renderDoc;
        std::optional<bool> xr;
        std::optional<bool> xrMirror;
    };

    LaunchOptions parseLaunchOptions(std::span<const std::string> args);
    std::optional<std::filesystem::path> findDefaultVpk(const LaunchOptions& options);
    void printUsage();
    int  runCliOnly(const LaunchOptions& options);
} // namespace vultra_app
