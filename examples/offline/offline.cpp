#include "../common/colored_mesh.hpp"
#include "color_gain.hpp"
#include "offline.hpp"

#include <vultra/core/base/logger.hpp>
#include <vultra/drivers/profiling/profiler.hpp>
#include <vultra/drivers/rhi/image_readback.hpp>
#include <vultra/servers/rendering/texture_blit.hpp>

#include <array>
#include <cmath>
#include <stdexcept>

void runOfflineExample(const OfflineOptions& options, const vultra::VpkArchive* shaders)
{
    using namespace vultra;
    if (!options.frames || options.output.empty() || std::filesystem::exists(options.output))
    {
        throw std::invalid_argument("Use finite positive frames and a fresh nonempty output directory");
    }
    if (!std::isfinite(options.gain) || options.gain < 0 || options.gain > 8)
    {
        throw std::invalid_argument("Color gain must be finite and in [0, 8]");
    }
    // Explicit composition: no application, window, renderer, asset importer or engine world.
    std::optional<ShaderProgram> meshProgram;
    std::optional<ShaderProgram> gainProgram;
    std::optional<ShaderProgram> displayProgram;
    if (shaders)
    {
        meshProgram    = ShaderProgram::load(shaders->read("mesh.vshaderc"), "mesh.vshaderc");
        gainProgram    = ShaderProgram::load(shaders->read("gain.vshaderc"), "gain.vshaderc");
        displayProgram = ShaderProgram::load(shaders->read("display.vshaderc"), "display.vshaderc");
    }
    Device              device;
    sample::ColoredMesh triangle(device,
                                 VriFormat_RGBA16_SFLOAT,
                                 sample::kTriangleVertices,
                                 sample::kTriangleIndices,
                                 VriPrimitiveTopology_TriangleList,
                                 false,
                                 meshProgram ? &*meshProgram : nullptr);
    TextureBlit         display(device, VriFormat_RGBA8_UNORM, 1, displayProgram ? &*displayProgram : nullptr);
    PassCatalog         catalog(device);
    catalog.add(research::colorGainDefinition(gainProgram ? &*gainProgram : nullptr));
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
    if (options.graph)
    {
        const std::array imports {GraphBinding {"scene.hdr", source}};
        built = options.graph->build(graph, catalog, imports);
        if (built.outputs.size() != 1)
        {
            throw std::invalid_argument("The offline example requires one marked HDR output");
        }
    }
    else
    {
        const std::array inputs {source};
        built.passes.push_back(catalog.build(graph, "research.color_gain", "gain", inputs, {{"gain", options.gain}}));
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
    for (uint64_t index = 0; index < options.frames; ++index)
    {
        auto* cmd = frame.begin();
        graph.execute(cmd, &profiler);
        frame.submitAndWait();
        profiler.collect();
    }
    // Float PFM stays linear; the display pass encodes sRGB exactly once for PNG.
    std::filesystem::create_directories(options.output);
    savePfm(readback(device, graph.getTexture(hdr)), options.output / "linear.pfm");
    savePng(readback(device, graph.getTexture(color)), options.output / "display.png");
    for (const auto& pass : profiler.timings())
    {
        Logger::app().info("{}: CPU {:.3f} ms, GPU {:.3f} ms", pass.name, pass.cpuMs, pass.gpuMs);
    }
    Logger::app().info("Completed {} offline frames; outputs in {}", options.frames, options.output.string());
}
