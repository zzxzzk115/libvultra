#include <vultra/assets/vpk_archive.hpp>
#include <vultra/core/base/command_line.hpp>
#include <vultra/core/base/logger.hpp>

int main(int argc, char** argv)
try
{
    argparse::ArgumentParser cli("vultra-pack", "0.1.0", argparse::default_arguments::none);
    vultra::addAppOptions(cli);
    cli.add_description("Cook and pack a project, pack builtins, or embed a project VPK into a runtime");
    cli.add_argument("inputs").nargs(argparse::nargs_pattern::at_least_one);
    cli.add_argument("--builtins").flag();
    cli.add_argument("--embed").flag();
    cli.add_argument("--engine").help("Engine root supplying builtin/shaders and external shader includes");
    cli.add_argument("--include").append().help("Additional project shader search path");
    if (!vultra::parseCommandLine(cli, argc, argv))
    {
        return 0;
    }
    const auto inputs   = cli.get<std::vector<std::string>>("inputs");
    const bool builtins = cli.get<bool>("--builtins");
    const bool embed    = cli.get<bool>("--embed");
    if ((builtins && embed) || inputs.size() != (embed ? 3 : 2))
    {
        throw std::invalid_argument("Use project.vproject output.vpk, --builtins engine-root output.vpk, or "
                                    "--embed runtime project.vpk output-exe");
    }
    if (builtins)
    {
        vultra::VpkArchive::packBuiltins(inputs[0], inputs[1]);
    }
    else if (embed)
    {
        vultra::VpkArchive::embedProject(inputs[0], inputs[1], inputs[2]);
    }
    else
    {
        vultra::ShaderCompileOptions options;
        if (const auto engine = cli.present<std::string>("--engine"))
        {
            const auto root            = std::filesystem::absolute(*engine);
            options.includeDirectories = {root / "builtin/shaders", root / "external"};
        }
        if (const auto roots = cli.present<std::vector<std::string>>("--include"))
        {
            for (const auto& root : *roots)
            {
                options.includeDirectories.emplace_back(std::filesystem::absolute(root));
            }
        }
        vultra::VpkArchive::packProject(inputs[0], inputs[1], options);
    }
    vultra::Logger::app().info("Created {}", inputs.back());
    return 0;
}
catch (const std::exception& error)
{
    vultra::Logger::app().error("{}", error.what());
    return 1;
}
