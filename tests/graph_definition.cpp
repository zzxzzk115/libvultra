#include "../examples/research/color_gain.hpp"

#include <vultra/servers/rendering/builtin/tone_mapping_pass.hpp>
#include <vultra/servers/rendering/graph/graph_definition.hpp>
#include <vultra/servers/rendering/research/capture.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
    using namespace vultra;

    void require(bool value, const char* message)
    {
        if (!value)
        {
            throw std::runtime_error(message);
        }
    }

    void expectError(const std::function<void()>& action, std::string_view expected)
    {
        try
        {
            action();
        }
        catch (const std::invalid_argument& error)
        {
            require(std::string_view(error.what()).find(expected) != std::string_view::npos,
                    "Graph error did not identify the failed boundary");
            return;
        }
        throw std::runtime_error("Invalid graph was accepted");
    }

    GraphDefinition chain()
    {
        // File order deliberately differs from dependency order.
        return {{{"final", "research.color_gain", {{"gain", 0.25}}}, {"boost", "research.color_gain", {{"gain", 2}}}},
                {{"scene.hdr", "boost.source"}, {"boost.color", "final.source"}},
                {"final.color"}};
    }

    Image render(Device& device, const PassCatalog& catalog, const GraphDefinition* definition)
    {
        GraphBuild   state;
        RenderGraph  graph(device);
        const Extent extent {13, 9};
        const auto   source = graph.createTexture("scene_hdr", colorTexture(extent, VriFormat_RGBA16_SFLOAT));
        graph.addPass("Scene",
                      {{source, Usage::eColorWrite}},
                      [&](auto* cmd, auto& resources)
                      {
                          const float color[4] {0.25f, 0.5f, 4, 0.75f};
                          beginColorPass(device, cmd, resources.getTexture(source).view(), extent, color);
                          device.core.CmdEndRendering(cmd);
                      });
        RenderGraph::Resource output {};
        if (definition)
        {
            const std::array imports {GraphBinding {"scene.hdr", source}};
            state  = definition->build(graph, catalog, imports);
            output = state.outputs.front().resource;
        }
        else
        {
            const std::array firstInput {source};
            auto             first = catalog.build(graph, "research.color_gain", "boost", firstInput, {{"gain", 2}});
            const std::array secondInput {first.outputs.front()};
            auto second = catalog.build(graph, "research.color_gain", "final", secondInput, {{"gain", 0.25}});
            output      = second.outputs.front();
            state.passes.push_back(std::move(first));
            state.passes.push_back(std::move(second));
            graph.exportResource(output);
        }
        graph.compile();
        require(graph.activePasses() == std::vector<std::string> {"Scene", "boost", "final"},
                "Graph definition did not resolve dependency order into the existing graph");
        require(graph.resourceInfo(output).active && graph.resourceInfo(output).exported,
                "Output metadata lost compiled liveness");
        Frame frame(device);
        Image previous;
        for (size_t i = 0; i < 3; ++i)
        {
            // BuiltPass moves must keep the captured parameter span valid, including subsequent live edits.
            auto& last = state.passes.back();
            catalog.setParameters(last, {{"gain", i == 0 ? 0.5 : 0.25}});
            const auto* values = last.parameterValues.data();
            expectError(
                [&]
                {
                    catalog.setParameters(last, {{"gain", 9}});
                },
                "final");
            require(last.parameterValues.data() == values && last.parameterValues.front() == (i == 0 ? 0.5 : 0.25),
                    "Rejected parameter update changed values or invalidated callbacks");
            graph.execute(frame.begin());
            frame.submitAndWait();
            auto             image = readback(device, graph.getTexture(output));
            const float      scale = i == 0 ? 2.0f : 1.0f;
            const std::array expected {0.125f * scale, 0.25f * scale, 2.0f * scale, 0.75f};
            for (size_t channel = 0; channel < image.rgba.size(); ++channel)
            {
                require(std::isfinite(image.rgba[channel]) &&
                            std::abs(image.rgba[channel] - expected[channel % 4]) < 0.0001f,
                        "Compute pass changed HDR range, alpha or a boundary texel");
            }
            if (i == 1)
            {
                require(previous.rgba != image.rgba, "Live parameter update did not change the compiled graph output");
            }
            else if (i == 2)
            {
                require(previous.rgba == image.rgba, "Stable graph changed across completed frames");
            }
            previous = std::move(image);
        }
        return previous;
    }

    Image renderTone(Device& device, const PassCatalog& catalog, bool definitionDriven)
    {
        ToneMappingPass tone(device);
        GraphBuild      build;
        RenderGraph     graph(device);
        const Extent    size {13, 9};
        const auto      hdr = graph.createTexture("hdr", colorTexture(size, VriFormat_RGBA16_SFLOAT));
        graph.addPass("Clear HDR",
                      {{hdr, Usage::eColorWrite}},
                      [&](auto* cmd, auto& current)
                      {
                          const float color[4] {0.25f, 0.5f, 4, 0.75f};
                          beginColorPass(device, cmd, current.getTexture(hdr).view(), size, color);
                          device.core.CmdEndRendering(cmd);
                      });
        RenderGraph::Resource       color;
        const std::array<double, 2> parameters {0, 0};
        if (definitionDriven)
        {
            const auto definition = GraphDefinition::parse(
                R"({"format":"vultra.graph","version":1,"passes":[{"id":"tone","type":"vultra.tone_mapping","parameters":{}}],"edges":[{"from":"scene.hdr","to":"tone.hdr"}],"outputs":["tone.color"]})");
            const std::array imports {GraphBinding {"scene.hdr", hdr}};
            build = definition.build(graph, catalog, imports);
            color = build.outputs.front().resource;
        }
        else
        {
            const std::array inputs {hdr};
            color = tone.addPasses(graph, "tone", inputs, parameters).front();
            graph.exportResource(color);
        }
        graph.compile();
        Frame frame(device);
        graph.execute(frame.begin());
        frame.submitAndWait();
        const auto result = readback(device, graph.getTexture(color));
        require(std::abs(result.rgba[0] - 0.6454f) < 0.005f && std::abs(result.rgba[1] - 0.8073f) < 0.005f &&
                    std::abs(result.rgba[2] - 0.9882f) < 0.005f && result.rgba[3] == 1,
                "Catalog tone mapping lost the ACES fit, display transfer or opaque output");
        return result;
    }

    void invalidGraphs(Device& device, PassCatalog& catalog)
    {
        RenderGraph      graph(device);
        const auto       source = graph.createTexture("input", colorTexture({4, 4}, VriFormat_RGBA16_SFLOAT));
        const std::array imports {GraphBinding {"scene.hdr", source}};
        GraphDefinition  unbound {{{"geometry", "vultra.gbuffer", {}}}, {}, {"geometry.normal_roughness"}};
        expectError(
            [&]
            {
                unbound.build(graph, catalog, {});
            },
            "bindBuiltinRasterPasses");
        auto invalid          = chain();
        invalid.edges[0].from = "final.color";
        expectError(
            [&]
            {
                invalid.build(graph, catalog, imports);
            },
            "cycle");
        invalid = chain();
        invalid.edges.pop_back();
        expectError(
            [&]
            {
                invalid.build(graph, catalog, imports);
            },
            "final.source");
        invalid = chain();
        invalid.edges.push_back(invalid.edges.front());
        expectError(
            [&]
            {
                invalid.build(graph, catalog, imports);
            },
            "boost.source");
        invalid               = chain();
        invalid.edges[0].from = "scene.missing";
        expectError(
            [&]
            {
                invalid.build(graph, catalog, imports);
            },
            "scene.missing");
        invalid         = chain();
        invalid.outputs = {"boost.missing"};
        expectError(
            [&]
            {
                invalid.build(graph, catalog, imports);
            },
            "boost.missing");
        invalid              = chain();
        invalid.passes[1].id = "final";
        expectError(
            [&]
            {
                invalid.build(graph, catalog, imports);
            },
            "duplicate");
        const std::array inputs {source};
        expectError(
            [&]
            {
                catalog.build(graph, "research.color_gain", "gain", inputs, {{"gaim", 1}});
            },
            "gaim");
        expectError(
            [&]
            {
                catalog.build(graph, "research.color_gain", "gain", inputs, {{"gain", 9}});
            },
            "gain");
        expectError(
            [&]
            {
                catalog.build(graph,
                              "research.color_gain",
                              "gain",
                              inputs,
                              {{"gain", std::numeric_limits<double>::quiet_NaN()}});
            },
            "gain");
        const auto       wrongFormat = graph.createTexture("unorm", colorTexture({4, 4}));
        const std::array unormInput {wrongFormat};
        expectError(
            [&]
            {
                catalog.build(graph, "research.color_gain", "gain", unormInput);
            },
            "gain.source");
        const auto buffer =
            graph.createBuffer("buffer", {64, 0, VriBufferUsage_StorageBuffer, VriMemoryLocation_Device});
        const std::array bufferInput {buffer};
        expectError(
            [&]
            {
                catalog.build(graph, "research.color_gain", "gain", bufferInput);
            },
            "gain.source");
        auto matchingInputs = research::colorGainDefinition();
        matchingInputs.type = "matching_inputs";
        matchingInputs.inputs.push_back({"reference", PassResourceKind::eTexture, VriFormat_RGBA16_SFLOAT, 0});
        catalog.add(std::move(matchingInputs));
        const auto differentSize = graph.createTexture("different_size", colorTexture({8, 4}, VriFormat_RGBA16_SFLOAT));
        const std::array mismatchedInputs {source, differentSize};
        expectError(
            [&]
            {
                catalog.build(graph, "matching_inputs", "extent", mismatchedInputs);
            },
            "extent.reference");
        RenderGraph foreignGraph(device);
        const auto foreignSource = foreignGraph.createTexture("foreign", colorTexture({4, 4}, VriFormat_RGBA16_SFLOAT));
        const std::array foreignInput {foreignSource};
        expectError(
            [&]
            {
                catalog.build(graph, "research.color_gain", "gain", foreignInput);
            },
            "another graph");
        auto unavailable             = research::colorGainDefinition();
        unavailable.type             = "unavailable";
        unavailable.requiredFeatures = uint64_t(1) << 63;
        catalog.add(std::move(unavailable));
        expectError(
            [&]
            {
                catalog.build(graph, "unavailable", "unsupported", inputs);
            },
            "VRI features");
        expectError(
            [&]
            {
                catalog.add(research::colorGainDefinition());
            },
            "Duplicate pass type");
        expectError(
            [&]
            {
                GraphDefinition::parse(R"({"format":"vultra.graph","version":2})");
            },
            "version 1");
        expectError(
            [&]
            {
                GraphDefinition::parse(R"({"format":"vultra.graph","version":1,"passes":false})");
            },
            "Invalid graph definition");
        expectError(
            [&]
            {
                GraphDefinition::parse(R"({"format":"vultra.graph","version":1,"passes":{},"edges":[],"outputs":[]})");
            },
            "arrays");
        require(graph.activePasses().empty(), "Rejected graphs recorded or scheduled GPU work");
    }
} // namespace

int main()
try
{
    Device      device;
    PassCatalog catalog(device);
    catalog.add(research::colorGainDefinition());
    invalidGraphs(device, catalog);
    const auto directory =
        std::filesystem::path("build/.tmp") /
        ("graph-definition-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    const auto original = chain();
    original.save(directory / "chain.vgraph");
    const auto loaded = GraphDefinition::load(directory / "chain.vgraph");
    require(loaded.serialize() == original.serialize(), "Graph definition save/load is not deterministic");
    const auto direct     = render(device, catalog, nullptr);
    const auto dataDriven = render(device, catalog, &loaded);
    require(direct.rgba == dataDriven.rgba, "C++ and definition graph GPU readbacks differ");
    require(renderTone(device, catalog, false).rgba == renderTone(device, catalog, true).rgba,
            "Direct built-in tone mapping and the catalog definition differ");
    std::cout << "C++ and version-1 definition outputs match with live parameter updates across completed frames\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
