#include "offline.hpp"

#include <vultra/core/base/command_line.hpp>
#include <vultra/core/base/logger.hpp>

#include <filesystem>
#include <system_error>

namespace
{
    // With --shaders, the graph file names an entry of that VPK.
    std::expected<vultra::GraphDefinition, vultra::Error> loadGraph(const vultra::VpkArchive* shaders,
                                                                    const std::string&        file)
    {
        if (!shaders)
        {
            return vultra::GraphDefinition::load(file);
        }
        const auto bytes = shaders->read(file);
        if (!bytes)
        {
            return std::unexpected(bytes.error());
        }
        return vultra::GraphDefinition::parse(
            std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
    }

    std::expected<void, vultra::Error> run(const argparse::ArgumentParser& cli)
    {
        using namespace vultra;
        const auto frames = cli.present<uint64_t>("--frames").value_or(3);
        if (!frames)
        {
            return makeError("Use a finite positive frame count");
        }
        if (cli.is_used("--gain") && cli.is_used("--graph"))
        {
            return makeError("Set gain in the graph definition when using --graph");
        }
        const std::filesystem::path output(cli.get<std::string>("--output"));
        std::error_code             existing;
        if (output.empty() || std::filesystem::exists(output, existing) || existing)
        {
            return makeError("Use a fresh nonempty output directory");
        }

        std::optional<VpkArchive> shaders;
        if (const auto file = cli.present<std::string>("--shaders"))
        {
            auto archive = VpkArchive::create(*file);
            if (!archive)
            {
                return std::unexpected(archive.error());
            }
            shaders = std::move(*archive);
        }
        OfflineOptions options {.frames = frames, .output = output, .gain = cli.get<double>("--gain")};
        if (const auto file = cli.present<std::string>("--graph"))
        {
            auto graph = loadGraph(shaders ? &*shaders : nullptr, *file);
            if (!graph)
            {
                return std::unexpected(graph.error());
            }
            options.graph = std::move(*graph);
        }
        return runOfflineExample(options, shaders ? &*shaders : nullptr);
    }
} // namespace

int main(int argc, char** argv)
{
    argparse::ArgumentParser cli("example-offline", "0.1.0", argparse::default_arguments::none);
    cli.add_description("Windowless VRI triangle, project compute pass and display conversion");
    vultra::addAppOptions(cli);
    cli.add_argument("--output").default_value(std::string("build/.tmp/example-offline"));
    cli.add_argument("--shaders").help("Load host-cooked shaders from a VPK; --graph then names a VPK entry");
    cli.add_argument("--graph").help("Use a .vgraph instead of the direct C++ pass definition");
    cli.add_argument("--gain").default_value(0.5).scan<'g', double>().help("Direct C++ color gain [0, 8]");
    const auto parsed = vultra::parseCommandLine(cli, argc, argv);
    if (!parsed)
    {
        vultra::Logger::app().error("{}", parsed.error().message);
        return 1;
    }
    if (!*parsed)
    {
        return 0;
    }
    if (auto result = run(cli); !result)
    {
        vultra::Logger::app().error("{}", result.error().message);
        return 1;
    }
    return 0;
}
