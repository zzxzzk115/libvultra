#include "launch_options.hpp"

#include <argparse/argparse.hpp>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <limits.h>
#include <mach-o/dyld.h>
#include <unistd.h>
#else
#include <limits.h>
#include <unistd.h>
#endif

namespace vultra_app
{
    namespace
    {
#ifndef VULTRA_APP_DEFAULT_VALIDATION
#define VULTRA_APP_DEFAULT_VALIDATION 0
#endif
#ifndef VULTRA_APP_DEFAULT_DEBUG_MARKERS
#define VULTRA_APP_DEFAULT_DEBUG_MARKERS 0
#endif
#ifndef VULTRA_APP_DEFAULT_RENDERDOC
#define VULTRA_APP_DEFAULT_RENDERDOC 0
#endif

        bool isCliCommand(const std::string& arg)
        {
            return arg == "help" || arg == "version" || arg == "pack" || arg == "import" || arg == "validate-vpk";
        }

        std::string normalizedRenderMode(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            if (value == "visible" || value == "offscreen" || value == "none")
                return value;
            std::cerr << "Argument warning: unknown --render-mode '" << value << "', using visible.\n";
            return "visible";
        }

        std::filesystem::path currentExecutablePath()
        {
#if defined(_WIN32)
            std::wstring buffer(MAX_PATH, L'\0');
            DWORD        size = 0;
            for (;;)
            {
                size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
                if (size == 0)
                    return {};
                if (size < buffer.size() - 1)
                    break;
                buffer.resize(buffer.size() * 2);
            }
            return std::filesystem::path(std::wstring(buffer.data(), size));
#elif defined(__APPLE__)
            uint32_t size = 0;
            _NSGetExecutablePath(nullptr, &size);
            std::vector<char> buffer(size + 1, '\0');
            if (_NSGetExecutablePath(buffer.data(), &size) != 0)
                return {};
            std::error_code ec;
            return std::filesystem::weakly_canonical(buffer.data(), ec);
#else
            std::vector<char> buffer(PATH_MAX, '\0');
            const ssize_t     size = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
            if (size <= 0)
                return {};
            buffer[static_cast<size_t>(size)] = '\0';
            return std::filesystem::path(buffer.data());
#endif
        }
    } // namespace

    LaunchOptions parseLaunchOptions(std::span<const std::string> args)
    {
        LaunchOptions options;
        options.validation   = VULTRA_APP_DEFAULT_VALIDATION != 0;
        options.debugMarkers = VULTRA_APP_DEFAULT_DEBUG_MARKERS != 0;
        options.renderDoc    = VULTRA_APP_DEFAULT_RENDERDOC != 0;
        if (!args.empty() && isCliCommand(args.front()))
        {
            options.cliOnly    = true;
            options.cliCommand = args.front();
            return options;
        }

        argparse::ArgumentParser program("vultra", "0.1.0", argparse::default_arguments::none);
        program.add_argument("-h", "--help").flag();
        program.add_argument("--editor").flag();
        program.add_argument("--mcp").flag();
        program.add_argument("--rpc").flag();
        program.add_argument("--mcp-host").default_value(std::string {});
        program.add_argument("--mcp-port").scan<'i', int>().default_value(0);
        program.add_argument("--render-mode").default_value(std::string {"visible"});
        program.add_argument("--project").default_value(std::string {});
        program.add_argument("--vpk").default_value(std::string {});
        program.add_argument("--scene").default_value(std::string {});
        program.add_argument("--plugins-dir").default_value(std::string {});
        program.add_argument("--export").flag();
        program.add_argument("--export-output", "--out").default_value(std::string {});
        program.add_argument("--export-platform").default_value(std::string {});
        program.add_argument("--export-run").flag();
        program.add_argument("--backend", "--render-backend").default_value(std::string {});
        program.add_argument("--render-profile").default_value(std::string {});
        program.add_argument("--validation").flag();
        program.add_argument("--no-validation").flag();
        program.add_argument("--debug-markers").flag();
        program.add_argument("--no-debug-markers").flag();
        program.add_argument("--renderdoc").flag();
        program.add_argument("--no-renderdoc").flag();
        program.add_argument("--xr").flag();
        program.add_argument("--no-xr").flag();
        program.add_argument("--xr-mirror").flag();
        program.add_argument("--no-xr-mirror").flag();

        std::vector<std::string> argv;
        argv.emplace_back("vultra");
        argv.insert(argv.end(), args.begin(), args.end());

        try
        {
            program.parse_args(argv);
            options.showHelp    = program.get<bool>("--help");
            options.editorMode  = program.get<bool>("--editor");
            options.mcpMode     = program.get<bool>("--mcp") || program.get<bool>("--rpc");
            options.renderMode  = normalizedRenderMode(program.get<std::string>("--render-mode"));
            options.projectPath = program.get<std::string>("--project");
            options.vpkPath     = program.get<std::string>("--vpk");
            options.sceneUri    = program.get<std::string>("--scene");
            options.pluginsDir  = program.get<std::string>("--plugins-dir");
            options.exportMode     = program.get<bool>("--export");
            options.exportOutput   = program.get<std::string>("--export-output");
            options.exportPlatform = program.get<std::string>("--export-platform");
            options.exportRun      = program.get<bool>("--export-run");
            if (const auto mcpHost = program.get<std::string>("--mcp-host"); !mcpHost.empty())
                options.mcpHost = mcpHost;
            if (const auto mcpPort = program.get<int>("--mcp-port"); mcpPort > 0)
                options.mcpPort = mcpPort;

            for (const auto& arg : args)
            {
                if (arg == "--validation")
                    options.validation = true;
                else if (arg == "--no-validation")
                    options.validation = false;
                else if (arg == "--debug-markers")
                    options.debugMarkers = true;
                else if (arg == "--no-debug-markers")
                    options.debugMarkers = false;
                else if (arg == "--renderdoc")
                    options.renderDoc = true;
                else if (arg == "--no-renderdoc")
                    options.renderDoc = false;
                else if (arg == "--xr")
                    options.xr = true;
                else if (arg == "--no-xr")
                    options.xr = false;
                else if (arg == "--xr-mirror")
                    options.xrMirror = true;
                else if (arg == "--no-xr-mirror")
                    options.xrMirror = false;
            }

            for (size_t i = 0; i < args.size(); ++i)
            {
                constexpr std::string_view kMcpPortEqPrefix {"--mcp-port="};
                const std::string_view arg = args[i];
                if (arg == "--mcp-port" && i + 1 < args.size())
                {
                    try
                    {
                        options.mcpPort = std::stoi(args[i + 1]);
                    }
                    catch (...)
                    {
                    }
                    ++i;
                }
                else if (arg.starts_with(kMcpPortEqPrefix))
                {
                    try
                    {
                        options.mcpPort = std::stoi(std::string(arg.substr(kMcpPortEqPrefix.size())));
                    }
                    catch (...)
                    {
                    }
                }
            }
        }
        catch (const std::exception& e)
        {
            std::cerr << "Argument error: " << e.what() << "\n\n";
            options.showHelp = true;
        }

        return options;
    }

    std::optional<std::filesystem::path> findDefaultVpk(const LaunchOptions& options)
    {
        namespace fs = std::filesystem;

        if (!options.vpkPath.empty())
        {
            fs::path explicitPath {options.vpkPath};
            if (fs::exists(explicitPath))
                return explicitPath.lexically_normal();
            return std::nullopt;
        }

        std::vector<fs::path> candidates;
        if (const auto exe = currentExecutablePath(); !exe.empty())
        {
            candidates.push_back(exe.parent_path() / (exe.stem().generic_string() + ".vpk"));
            candidates.push_back(fs::current_path() / (exe.stem().generic_string() + ".vpk"));
        }

        if (!options.projectPath.empty())
        {
            const fs::path project {options.projectPath};
            candidates.push_back(project / "resources.vpk");
            candidates.push_back(project / "resources" / "resources.vpk");
        }

        candidates.emplace_back("resources.vpk");
        candidates.emplace_back("resources/resources.vpk");

        for (const auto& candidate : candidates)
        {
            std::error_code ec;
            if (fs::exists(candidate, ec) && fs::is_regular_file(candidate, ec))
                return candidate.lexically_normal();
        }
        return std::nullopt;
    }

    void printUsage()
    {
        std::cout << "VultraEngine runtime\n\n"
                  << "Usage:\n"
                  << "  vultra [--vpk resources.vpk] [--scene res://scenes/main.vscn]\n"
                  << "  vultra --editor [--mcp|--rpc] [--mcp-port 8848] [--render-mode visible|offscreen|none] --project <project-dir>\n"
                  << "  vultra [--mcp|--rpc] --render-mode none --project <project-dir> [--scene res://scenes/main.vscn]\n"
                  << "  vultra [--mcp|--rpc] --render-mode offscreen --project <project-dir> [--scene res://scenes/main.vscn]\n"
                  << "  vultra [--no-xr] [--xr-mirror|--no-xr-mirror] --editor --project <project-dir>\n"
                  << "  vultra [--validation|--no-validation] [--debug-markers|--no-debug-markers] "
                     "[--renderdoc|--no-renderdoc]\n"
                  << "  vultra --project <project-dir>\n"
                  << "  vultra --export --project <project-dir> [--export-output <dir>] [--scene res://...] [--export-run]\n"
                  << "  vultra help | version\n\n"
                  << "Tool subcommands (integrated; bypass the engine UI):\n"
                  << "  vultra asset import <asset-root> [--reimport]\n"
                  << "        Import/reimport a project's source assets (cook shaders, textures, meshes,\n"
                  << "        scenes...) into <asset-root>/imported. --reimport forces a full recook.\n"
                  << "  vultra asset pack <asset-root> <out.vpk> [--zstd N] [--include logical/path] [--root res://...]\n"
                  << "        Bundle the imported assets into a .vpk package.\n"
                  << "  vultra asset validate-vpk <resources.vpk> [--asset-root <root>] [--registry <asset_registry.tsv>]\n"
                  << "        Verify a .vpk against its source asset tree.\n"
                  << "  vultra shader <vshaderc args>\n"
                  << "        Run the shader compiler CLI (build | compile | pack-glsl | wgsl | ...).\n"
                  << "        Try `vultra shader --help` for its options.\n"
                  << "  vultra mcp-stdio-bridge [--host 127.0.0.1] [--port 8848]\n"
                  << "        Stdio<->HTTP bridge to the editor's MCP server (launched by MCP clients).\n\n"
                  << "Notes:\n"
                  << "  Without --vpk, Vultra first tries <executable-name>.vpk next to the executable.\n"
                  << "  Without a VPK, Vultra opens the Project Launcher.\n"
                  << "  --editor requires --project; no-project editor sessions are invalid.\n"
                  << "  --rpc is an alias for --mcp; --render-mode=none runs no-window simulation services and disables visual capture tools.\n"
                  << "  --render-mode=offscreen hides the window but keeps render services active for visual capture.\n";
    }

    int runCliOnly(const LaunchOptions& options)
    {
        if (options.cliCommand.empty() || options.cliCommand == "help")
        {
            printUsage();
            return 0;
        }
        if (options.cliCommand == "version")
        {
            std::cout << "VultraEngine 0.1.0\n";
            return 0;
        }

        std::cerr << "Unknown CLI command '" << options.cliCommand
                  << "'. Use `vultra asset ...` or `vultra shader ...` for integrated tools.\n";
        return 2;
    }
} // namespace vultra_app
