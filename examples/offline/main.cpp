#include "offline.hpp"

#include <vultra/core/base/command_line.hpp>
#include <vultra/core/base/logger.hpp>

#include <filesystem>
#include <stdexcept>

int main(int argc, char** argv)
try
{
    using namespace vultra;
    argparse::ArgumentParser cli("example-offline", "0.1.0", argparse::default_arguments::none);
    cli.add_description("Windowless VRI triangle, project compute pass and display conversion");
    addAppOptions(cli);
    cli.add_argument("--output").default_value(std::string("build/.tmp/example-offline"));
    cli.add_argument("--shaders").help("Load host-cooked shaders from a VPK; --graph then names a VPK entry");
    cli.add_argument("--graph").help("Use a .vgraph instead of the direct C++ pass definition");
    cli.add_argument("--gain").default_value(0.5).scan<'g', double>().help("Direct C++ color gain [0, 8]");
    if (!parseCommandLine(cli, argc, argv))
    {
        return 0;
    }
    const auto frames = cli.present<uint64_t>("--frames").value_or(3);
    if (!frames)
    {
        throw std::invalid_argument("Use a finite positive frame count");
    }
    if (cli.is_used("--gain") && cli.is_used("--graph"))
    {
        throw std::invalid_argument("Set gain in the graph definition when using --graph");
    }
    const std::filesystem::path output(cli.get<std::string>("--output"));
    if (output.empty() || std::filesystem::exists(output))
    {
        throw std::invalid_argument("Use a fresh nonempty output directory");
    }

    std::optional<VpkArchive> shaders;
    if (const auto file = cli.present<std::string>("--shaders"))
    {
        shaders.emplace(*file);
    }
    OfflineOptions options {.frames = frames, .output = output, .gain = cli.get<double>("--gain")};
    if (const auto file = cli.present<std::string>("--graph"))
    {
        if (shaders)
        {
            const auto bytes = shaders->read(*file);
            options.graph =
                GraphDefinition::parse(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
        }
        else
        {
            options.graph = GraphDefinition::load(*file);
        }
    }
    runOfflineExample(options, shaders ? &*shaders : nullptr);
    return 0;
}
catch (const std::exception& error)
{
    vultra::Logger::app().error("{}", error.what());
    return 1;
}
