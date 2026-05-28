#include <vultra/function/material_graph/material_graph.hpp>
#include <vultra/function/material_graph/material_graph_compiler.hpp>
#include <vultra/function/material_graph/material_node_registry.hpp>

#include <cstdlib>
#include <iostream>
#include <string_view>

using namespace vultra::material_graph;

namespace
{
    void require(bool condition, std::string_view message)
    {
        if (condition)
            return;
        std::cerr << "material_graph test failed: " << message << '\n';
        std::exit(1);
    }

    bool hasErrorFor(const std::vector<Diagnostic>& diagnostics, std::string_view nodeId)
    {
        for (const auto& diagnostic : diagnostics)
            if (diagnostic.severity == Diagnostic::Severity::eError && diagnostic.nodeId == nodeId)
                return true;
        return false;
    }

    Graph makeColorOnlyGraph()
    {
        Graph graph;
        graph.version = 1;
        graph.domain  = Domain::eSurface;
        graph.name    = "Color Only";
        graph.nodes.push_back(Node {
            .typeId      = "vultra.param.color",
            .id          = "color",
            .displayName = "Color",
            .params      = {{"value", {0.25f, 0.5f, 0.75f, 1.0f}}},
        });
        graph.nodes.push_back(Node {
            .typeId      = "vultra.output.surface",
            .id          = "out",
            .displayName = "Surface Output",
            .params      = {{"shadingModel", "Unlit"}, {"alphaMode", "Mask"}, {"alphaCutoff", 0.4f}},
        });
        graph.links.push_back(Link {
            .from = {.nodeId = "color", .pin = "value"},
            .to   = {.nodeId = "out", .pin = "baseColor"},
        });
        return graph;
    }
} // namespace

int main()
{
    {
        const auto              text = R"json({
            "version": 1,
            "domain": "surface",
            "name": "Roundtrip",
            "nodes": [],
            "links": [],
            "metadata": {"author": "test"},
            "futureField": {"kept": true}
        })json";
        std::vector<Diagnostic> diagnostics;
        auto                    graph = loadGraphFromText(text, &diagnostics);
        require(graph.has_value(), "valid graph JSON should load");
        require(graph->unknown.contains("futureField"), "unknown root fields should be preserved");

        const auto json = graphToJson(*graph);
        require(json.contains("futureField"), "unknown root fields should survive serialization");
        require(json["futureField"]["kept"].get<bool>(), "unknown root field value should survive serialization");
        require(json["version"].get<uint32_t>() == 1, "schema version should roundtrip");
    }

    {
        NodeRegistry registry;
        require(registry.registerNode({.typeId = "test.node", .displayName = "Test"}),
                "first node registration should pass");
        require(!registry.registerNode({.typeId = "test.node", .displayName = "Duplicate"}),
                "duplicate node registration should fail");

        Graph graph = makeColorOnlyGraph();
        graph.nodes.push_back(Node {.typeId = "test.unknown", .id = "unknown"});
        const auto diagnostics = validateGraph(graph, makeBuiltinNodeRegistry());
        require(hasErrorFor(diagnostics, "unknown"), "unknown node should produce a node-scoped error");
    }

    {
        MaterialGraphCompiler  compiler;
        SurfaceFunctionBackend backend;
        const auto             graphId = stableGraphId("res://materials/color-only.vmatgraph");
        auto                   result  = compiler.compile(
            CompileInput {
                                   .graph    = makeColorOnlyGraph(),
                                   .shaderId = "folder/color-only.vmatgraph",
                                   .graphId  = graphId,
            },
            backend);
        require(result.has_value(), "color-only graph should compile");
        require(result->vshaderSource.find("eval_material_graph_folder_color_only_vmatgraph") != std::string::npos,
                "shader id should be sanitized into a GLSL-safe function name");
        require(result->vshaderSource.find("float timeSeconds") != std::string::npos,
                "compiled graph function should expose time input for dynamic material nodes");
        require(result->vshaderSource.find("surface.normalWS = normalize(normalWS);") != std::string::npos,
                "unconnected surface normal should use normalWS fallback");
        require(result->vshaderSource.find("surface.shadingModel = 1u;") != std::string::npos,
                "unlit shading model should compile into surface metadata");
        require(result->vshaderSource.find("surface.alphaMode = 1u;") != std::string::npos,
                "mask alpha mode should compile into surface metadata");
    }

    std::cout << "material_graph tests passed\n";
    return 0;
}
