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
        bool        mcpMode {false};
        std::string renderMode {"visible"};
        bool        cliOnly {false};
        std::string cliCommand;
        std::string projectPath;
        std::string vpkPath;
        std::string sceneUri;
        std::string pluginsDir;
        // Headless export: `--export --project <dir> --export-output <dir> [--export-run]`.
        bool        exportMode {false};
        std::string exportOutput;
        std::string exportPlatform; // empty -> host desktop platform
        bool        exportRun {false};
        std::optional<bool> validation;
        std::optional<bool> debugMarkers;
        std::optional<bool> renderDoc;
        std::optional<std::string> mcpHost;
        std::optional<int> mcpPort;
        std::optional<bool> xr;
        std::optional<bool> xrMirror;
    };

    LaunchOptions parseLaunchOptions(std::span<const std::string> args);
    std::optional<std::filesystem::path> findDefaultVpk(const LaunchOptions& options);
    void printUsage();
    int  runCliOnly(const LaunchOptions& options);
    // Headless desktop export (no editor window): packages the project into <output>/<name>.exe +
    // <name>.vpk (plugins bundled), optionally launching it when options.exportRun is set.
    int  runHeadlessExport(const LaunchOptions& options);
} // namespace vultra_app
