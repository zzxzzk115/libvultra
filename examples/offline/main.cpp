#include "../common/colored_mesh.hpp"
#include "../research/color_gain.hpp"

#include <vultra/core/base/command_line.hpp>
#include <vultra/core/base/logger.hpp>
#include <vultra/drivers/profiling/profiler.hpp>
#include <vultra/drivers/rhi/image_readback.hpp>
#include <vultra/servers/rendering/graph/graph_definition.hpp>
#include <vultra/servers/rendering/texture_blit.hpp>

#include <array>
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

    // Explicit composition: no application, window, renderer, asset importer or engine world.
    Device              device;
    sample::ColoredMesh triangle(device, VriFormat_RGBA16_SFLOAT, sample::kTriangleVertices, sample::kTriangleIndices);
    TextureBlit         display(device, VriFormat_RGBA8_UNORM);
    PassCatalog         catalog(device);
    catalog.add(research::colorGainDefinition());
    GraphBuild       built;
    RenderGraph      graph(device);
    constexpr Extent size {129, 97};
    const auto       source = graph.createTexture("scene.hdr", colorTexture(size, VriFormat_RGBA16_SFLOAT));
    graph.addPass("Triangle",
                  {{source, Usage::eColorWrite}},
                  [&](auto* cmd, auto& active)
                  {
                      const float clear[4] {0, 0, 0, 1};
                      triangle.draw(cmd, active.getTexture(source), clear);
                  });
    if (const auto file = cli.present<std::string>("--graph"))
    {
        const std::array imports {GraphBinding {"scene.hdr", source}};
        built = GraphDefinition::load(*file).build(graph, catalog, imports);
        if (built.outputs.size() != 1)
        {
            throw std::invalid_argument("The offline example requires one marked HDR output");
        }
    }
    else
    {
        const std::array inputs {source};
        built.passes.push_back(
            catalog.build(graph, "research.color_gain", "gain", inputs, {{"gain", cli.get<double>("--gain")}}));
        built.outputs.push_back({"gain.color", built.passes.front().outputs.front()});
    }
    const auto hdr   = built.outputs.front().resource;
    const auto color = graph.createTexture("display.color", colorTexture(size, VriFormat_RGBA8_UNORM));
    graph.addPass("Display",
                  {{hdr, Usage::eSampled}, {color, Usage::eColorWrite}},
                  [&](auto* cmd, auto& active)
                  {
                      display.draw(cmd, active.getTexture(color), {0, 0, size.width, size.height}, 0, true);
                  });
    graph.exportResource(hdr);
    graph.exportResource(color);
    graph.compile();
    display.setSource(0, graph.getTexture(hdr));

    Frame    frame(device);
    Profiler profiler(device);
    for (uint64_t index = 0; index < frames; ++index)
    {
        auto* cmd = frame.begin();
        graph.execute(cmd, &profiler);
        frame.submitAndWait();
        profiler.collect();
    }
    // Float PFM stays linear; the display pass encodes sRGB exactly once for PNG.
    std::filesystem::create_directories(output);
    savePfm(readback(device, graph.getTexture(hdr)), output / "linear.pfm");
    savePng(readback(device, graph.getTexture(color)), output / "display.png");
    for (const auto& pass : profiler.timings())
    {
        Logger::app().info("{}: CPU {:.3f} ms, GPU {:.3f} ms", pass.name, pass.cpuMs, pass.gpuMs);
    }
    Logger::app().info("Completed {} offline frames; outputs in {}", frames, output.string());
    return 0;
}
catch (const std::exception& error)
{
    vultra::Logger::app().error("{}", error.what());
    return 1;
}
