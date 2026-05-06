#include "launch_options.hpp"

#include <argparse/argparse.hpp>

#include <iostream>

namespace vultra_app
{
    namespace
    {
        bool isCliCommand(const std::string& arg)
        {
            return arg == "help" || arg == "version" || arg == "pack" || arg == "import" || arg == "validate-vpk";
        }
    } // namespace

    LaunchOptions parseLaunchOptions(std::span<const std::string> args)
    {
        LaunchOptions options;
        if (!args.empty() && isCliCommand(args.front()))
        {
            options.cliOnly    = true;
            options.cliCommand = args.front();
            return options;
        }

        argparse::ArgumentParser program("vultra", "0.1.0", argparse::default_arguments::none);
        program.add_argument("-h", "--help").flag();
        program.add_argument("--editor").flag();
        program.add_argument("--project").default_value(std::string {});
        program.add_argument("--vpk").default_value(std::string {});
        program.add_argument("--scene").default_value(std::string {"res://scenes/main.vscn"});

        std::vector<std::string> argv;
        argv.emplace_back("vultra");
        argv.insert(argv.end(), args.begin(), args.end());

        try
        {
            program.parse_args(argv);
            options.showHelp   = program.get<bool>("--help");
            options.editorMode = program.get<bool>("--editor");
            options.projectPath = program.get<std::string>("--project");
            options.vpkPath     = program.get<std::string>("--vpk");
            options.sceneUri    = program.get<std::string>("--scene");
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
            if (fs::exists(candidate))
                return candidate.lexically_normal();
        }
        return std::nullopt;
    }

    void printUsage()
    {
        std::cout << "VultraEngine runtime\n\n"
                  << "Usage:\n"
                  << "  vultra [--vpk resources.vpk] [--scene res://scenes/main.vscn]\n"
                  << "  vultra --editor --project <project-dir>\n"
                  << "  vultra --project <project-dir>\n"
                  << "  vultra help\n\n"
                  << "Notes:\n"
                  << "  Without a VPK, Vultra opens the Project Launcher.\n"
                  << "  CLI subcommands are reserved for the integrated tool workflow.\n";
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

        std::cerr << "CLI command '" << options.cliCommand
                  << "' is reserved but not wired yet. Use vasset-cli for asset commands for now.\n";
        return 2;
    }
} // namespace vultra_app
