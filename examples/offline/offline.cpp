#include "../common/colored_mesh.hpp"
#include "color_gain.hpp"
#include "offline.hpp"

#include <vultra/core/base/logger.hpp>
#include <vultra/gpu/image_readback.hpp>
#include <vultra/gpu/profiling/profiler.hpp>
#include <vultra/render/texture_blit.hpp>

#include <array>
#include <cmath>
#include <system_error>

namespace
{
    // Without a VPK, each pass compiles its Slang source instead.
    std::expected<std::optional<vultra::ShaderProgram>, vultra::Error> loadProgram(const vultra::VpkArchive* shaders,
                                                                                   std::string_view          entry)
    {
        if (!shaders)
        {
            return std::nullopt;
        }
        const auto bytes = shaders->read(entry);
        if (!bytes)
        {
            return std::unexpected(bytes.error());
        }
        return vultra::ShaderProgram::load(*bytes, entry);
    }
} // namespace

std::expected<void, vultra::Error> runOfflineExample(const OfflineOptions& options, const vultra::VpkArchive* shaders)
{
    using namespace vultra;
    std::error_code existing;
    if (!options.frames || options.output.empty() || std::filesystem::exists(options.output, existing) || existing)
    {
        return makeError("Use finite positive frames and a fresh nonempty output directory");
    }
    if (!std::isfinite(options.gain) || options.gain < 0 || options.gain > 8)
    {
        return makeError("Color gain must be finite and in [0, 8]");
    }
    // Explicit composition: no application, window, renderer, asset importer or engine world.
    const auto meshProgram = loadProgram(shaders, "mesh.vshaderc");
    if (!meshProgram)
    {
        return std::unexpected(meshProgram.error());
    }
    const auto gainProgram = loadProgram(shaders, "gain.vshaderc");
    if (!gainProgram)
    {
        return std::unexpected(gainProgram.error());
    }
    const auto displayProgram = loadProgram(shaders, "display.vshaderc");
    if (!displayProgram)
    {
        return std::unexpected(displayProgram.error());
    }
    auto device = Device::create();
    if (!device)
    {
        return std::unexpected(device.error());
    }
    auto triangle = sample::ColoredMesh::create(**device,
                                                VriFormat_RGBA16_SFLOAT,
                                                sample::kTriangleVertices,
                                                sample::kTriangleIndices,
                                                VriPrimitiveTopology_TriangleList,
                                                false,
                                                *meshProgram ? &**meshProgram : nullptr);
    if (!triangle)
    {
        return std::unexpected(triangle.error());
    }
    auto display =
        TextureBlit::create(**device, VriFormat_RGBA8_UNORM, 1, *displayProgram ? &**displayProgram : nullptr);
    if (!display)
    {
        return std::unexpected(display.error());
    }
    PassCatalog catalog(**device);
    if (auto added = catalog.add(research::colorGainDefinition(*gainProgram ? &**gainProgram : nullptr)); !added)
    {
        return added;
    }
    GraphBuild       built;
    RenderGraph      graph(**device);
    constexpr Extent size {129, 97};
    const auto       source = graph.createTexture("scene.hdr", colorTexture(size, VriFormat_RGBA16_SFLOAT));
    if (!source)
    {
        return std::unexpected(source.error());
    }
    if (auto added = graph.addPass("Triangle",
                                   {{*source, Usage::eColorWrite}},
                                   [&](auto* cmd, auto& active) -> std::expected<void, Error>
                                   {
                                       const float clear[4] {0, 0, 0, 1};
                                       return (*triangle)->draw(cmd, active.getTexture(*source), clear);
                                   });
        !added)
    {
        return added;
    }
    if (options.graph)
    {
        const std::array imports {GraphBinding {"scene.hdr", *source}};
        auto             fromDefinition = options.graph->build(graph, catalog, imports);
        if (!fromDefinition)
        {
            return std::unexpected(fromDefinition.error());
        }
        built = std::move(*fromDefinition);
        if (built.outputs.size() != 1)
        {
            return makeError("The offline example requires one marked HDR output");
        }
    }
    else
    {
        const std::array inputs {*source};
        auto             gain = catalog.build(graph, "research.color_gain", "gain", inputs, {{"gain", options.gain}});
        if (!gain)
        {
            return std::unexpected(gain.error());
        }
        built.passes.push_back(std::move(*gain));
        built.outputs.push_back({"gain.color", built.passes.front().outputs.front()});
    }
    const auto hdr   = built.outputs.front().resource;
    const auto color = graph.createTexture("display.color", colorTexture(size, VriFormat_RGBA8_UNORM));
    if (!color)
    {
        return std::unexpected(color.error());
    }
    if (auto added = graph.addPass(
            "Display",
            {{hdr, Usage::eSampled}, {*color, Usage::eColorWrite}},
            [&](auto* cmd, auto& active) -> std::expected<void, Error>
            {
                return (*display)->draw(cmd, active.getTexture(*color), {0, 0, size.width, size.height}, 0, true);
            });
        !added)
    {
        return added;
    }
    for (const auto resource : {hdr, *color})
    {
        if (auto exported = graph.exportResource(resource); !exported)
        {
            return exported;
        }
    }
    if (auto compiled = graph.compile(); !compiled)
    {
        return compiled;
    }
    if (auto bound = (*display)->setSource(0, graph.getTexture(hdr)); !bound)
    {
        return bound;
    }

    auto frame = Frame::create(**device);
    if (!frame)
    {
        return std::unexpected(frame.error());
    }
    auto profiler = Profiler::create(**device);
    if (!profiler)
    {
        return std::unexpected(profiler.error());
    }
    for (uint64_t index = 0; index < options.frames; ++index)
    {
        const auto cmd = (*frame)->begin();
        if (!cmd)
        {
            return std::unexpected(cmd.error());
        }
        if (auto executed = graph.execute(*cmd, profiler->get()); !executed)
        {
            return executed;
        }
        if (auto submitted = (*frame)->submitAndWait(); !submitted)
        {
            return submitted;
        }
        if (auto collected = (*profiler)->collect(); !collected)
        {
            return collected;
        }
    }
    // Float PFM stays linear; the display pass encodes sRGB exactly once for PNG.
    std::error_code created;
    std::filesystem::create_directories(options.output, created);
    if (created)
    {
        return makeError("Create {}: {}", options.output.string(), created.message());
    }
    const auto linear = readback(**device, graph.getTexture(hdr));
    if (!linear)
    {
        return std::unexpected(linear.error());
    }
    if (auto saved = savePfm(*linear, options.output / "linear.pfm"); !saved)
    {
        return saved;
    }
    const auto encoded = readback(**device, graph.getTexture(*color));
    if (!encoded)
    {
        return std::unexpected(encoded.error());
    }
    if (auto saved = savePng(*encoded, options.output / "display.png"); !saved)
    {
        return saved;
    }
    for (const auto& pass : (*profiler)->timings())
    {
        Logger::app().info("{}: CPU {:.3f} ms, GPU {:.3f} ms", pass.name, pass.cpuMs, pass.gpuMs);
    }
    Logger::app().info("Completed {} offline frames; outputs in {}", options.frames, options.output.string());
    return {};
}
