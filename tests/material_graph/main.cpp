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
        const auto              text = R"json({
            "version": 1,
            "domain": "surface",
            "nodes": [
                {
                    "typeId": "vultra.output.surface",
                    "id": "out",
                    "params": {"baseColor": [1.0, 0.0, 0.0, 1.0]}
                }
            ],
            "links": []
        })json";
        std::vector<Diagnostic> diagnostics;
        auto                    graph = loadGraphFromText(text, &diagnostics);
        require(graph.has_value(), "graph JSON using typeId should load");
        require(graph->nodes.size() == 1, "typeId should keep the node");
        require(graph->nodes.front().typeId == "vultra.output.surface", "typeId should populate node type");
    }

    {
        const auto              text = R"json({
            "version": 1,
            "domain": "surface",
            "nodes": [
                {"typeId": "vultra.param.color", "id": "color"},
                {"typeId": "vultra.output.surface", "id": "out"}
            ],
            "links": [
                {
                    "from": {"nodeId": "color", "pin": "value"},
                    "to": {"nodeId": "out", "pin": "baseColor"}
                }
            ]
        })json";
        std::vector<Diagnostic> diagnostics;
        auto                    graph = loadGraphFromText(text, &diagnostics);
        require(graph.has_value(), "graph JSON using nodeId link endpoints should load");
        require(graph->links.size() == 1, "nodeId link endpoints should preserve link");
        require(graph->links.front().from.nodeId == "color", "nodeId should populate link source node");
        require(graph->links.front().to.nodeId == "out", "nodeId should populate link target node");

        const auto json = graphToJson(*graph);
        require(json["nodes"][0].contains("typeId"), "graph save should write canonical typeId");
        require(!json["nodes"][0].contains("type"), "graph save should not write legacy type");
        require(json["links"][0]["from"].contains("nodeId"), "graph save should write canonical source nodeId");
        require(json["links"][0]["to"].contains("nodeId"), "graph save should write canonical target nodeId");
        require(!json["links"][0]["from"].contains("node"), "graph save should not write legacy source node");
        require(!json["links"][0]["to"].contains("node"), "graph save should not write legacy target node");
    }

    {
        const auto              text = R"json({
            "version": 1,
            "domain": "surface",
            "nodes": [
                {"type": "vultra.param.color", "id": "color"},
                {"type": "vultra.output.surface", "id": "out"}
            ],
            "links": [
                {
                    "from": {"node": "color", "pin": "value"},
                    "to": {"node": "out", "pin": "baseColor"}
                }
            ]
        })json";
        std::vector<Diagnostic> diagnostics;
        auto                    graph = loadGraphFromText(text, &diagnostics);
        require(graph.has_value(), "legacy graph JSON using type/node should still load");
        require(graph->nodes.front().typeId == "vultra.param.color", "legacy type should populate node typeId");
        require(graph->links.size() == 1, "legacy node link endpoints should preserve link");
        require(graph->links.front().to.nodeId == "out", "legacy node should populate link nodeId");
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
        const auto nodeText = R"json({
            "type": "MaterialGraphNode",
            "version": 1,
            "typeId": "project.tint",
            "displayName": "Project Tint",
            "inputs": [
                {"name": "color", "type": "color", "defaultValue": [1.0, 1.0, 1.0, 1.0]},
                {"name": "amount", "type": "float", "defaultValue": 1.0}
            ],
            "outputs": [
                {"name": "out", "type": "color"}
            ],
            "defaultParams": {"amount": 0.5},
            "implementation": {
                "language": "glsl",
                "outputs": {
                    "out": "mix({{input:color}}, vec4(1.0, 0.0, 0.0, 1.0), {{param:amount}})"
                }
            }
        })json";
        auto parsed = loadNodeDescriptorFromText(nodeText);
        require(parsed.ok(), "valid custom node descriptor should parse");
        require(parsed.descriptor.typeId == "project.tint", "custom node type id should parse");
        require(parsed.descriptor.inputs.size() == 2, "custom node inputs should parse");
        require(parsed.descriptor.outputs.size() == 1, "custom node outputs should parse");
        require(parsed.descriptor.defaultParams["amount"].get<float>() == 0.5f,
                "custom node default params should parse");
        require(parsed.descriptor.implementation["outputs"]["out"].is_string(),
                "custom node GLSL output expression should parse");

        auto registry = makeBuiltinNodeRegistry();
        require(registry.registerNode(std::move(parsed.descriptor)), "custom node should register");
        Graph graph = makeColorOnlyGraph();
        graph.nodes.push_back(Node {.typeId = "project.tint", .id = "tint"});
        const auto diagnostics = validateGraph(graph, registry);
        require(!hasErrorFor(diagnostics, "tint"), "registered custom node should validate");

        Graph compileGraph;
        compileGraph.version = 1;
        compileGraph.domain  = Domain::eSurface;
        compileGraph.nodes.push_back(Node {
            .typeId      = "vultra.param.color",
            .id          = "color",
            .displayName = "Color",
            .params      = {{"value", {0.0f, 0.0f, 1.0f, 1.0f}}},
        });
        compileGraph.nodes.push_back(Node {
            .typeId      = "project.tint",
            .id          = "tint",
            .displayName = "Project Tint",
        });
        compileGraph.nodes.push_back(Node {
            .typeId      = "vultra.output.surface",
            .id          = "out",
            .displayName = "Surface Output",
            .params      = {{"shadingModel", "Unlit"}},
        });
        compileGraph.links.push_back(Link {
            .from = {.nodeId = "color", .pin = "value"},
            .to   = {.nodeId = "tint", .pin = "color"},
        });
        compileGraph.links.push_back(Link {
            .from = {.nodeId = "tint", .pin = "out"},
            .to   = {.nodeId = "out", .pin = "baseColor"},
        });

        MaterialGraphCompiler compiler {registry};
        SurfaceFunctionBackend backend;
        auto result = compiler.compile(
            CompileInput {
                           .graph    = compileGraph,
                           .shaderId = "custom/tint",
                           .graphId  = stableGraphId("res://materials/custom-tint.vmatgraph.json"),
            },
            backend);
        require(result.has_value(), "custom snippet node graph should compile");
        require(result->vshaderSource.find("mix(") != std::string::npos,
                "custom snippet node expression should be emitted");
        require(result->vshaderSource.find("vec4(1.0, 0.0, 0.0, 1.0)") != std::string::npos,
                "custom snippet literal should be preserved");
    }

    {
        const auto tintText = R"json({
            "type": "MaterialGraphNode",
            "version": 1,
            "typeId": "project.tint_green",
            "displayName": "Project Tint Green",
            "inputs": [
                {"name": "color", "type": "color", "defaultValue": [1.0, 1.0, 1.0, 1.0]},
                {"name": "amount", "type": "float", "defaultValue": 0.25}
            ],
            "outputs": [
                {"name": "out", "type": "color"}
            ],
            "defaultParams": {"amount": 0.25},
            "implementation": {
                "language": "glsl",
                "outputs": {
                    "out": "mix({{input:color}}, vec4(0.0, 1.0, 0.0, 1.0), {{param:amount}})"
                }
            }
        })json";
        const auto boostText = R"json({
            "type": "MaterialGraphNode",
            "version": 1,
            "typeId": "project.boost_color",
            "displayName": "Project Boost Color",
            "inputs": [
                {"name": "color", "type": "color", "defaultValue": [1.0, 1.0, 1.0, 1.0]},
                {"name": "multiplier", "type": "float", "defaultValue": 1.0}
            ],
            "outputs": [
                {"name": "out", "type": "color"}
            ],
            "defaultParams": {"multiplier": 1.5},
            "implementation": {
                "language": "glsl",
                "outputs": {
                    "out": "clamp({{input:color}} * {{param:multiplier}}, vec4(0.0), vec4(1.0))"
                }
            }
        })json";

        auto tintParsed  = loadNodeDescriptorFromText(tintText);
        auto boostParsed = loadNodeDescriptorFromText(boostText);
        require(tintParsed.ok(), "first project custom node descriptor should parse");
        require(boostParsed.ok(), "second project custom node descriptor should parse");

        auto registry = makeBuiltinNodeRegistry();
        require(registry.registerNode(std::move(tintParsed.descriptor)), "first project custom node should register");
        require(registry.registerNode(std::move(boostParsed.descriptor)), "second project custom node should register");

        Graph compileGraph;
        compileGraph.version = 1;
        compileGraph.domain  = Domain::eSurface;
        compileGraph.nodes.push_back(Node {
            .typeId      = "vultra.param.color",
            .id          = "color",
            .displayName = "Color",
            .params      = {{"value", {0.0f, 0.0f, 1.0f, 1.0f}}},
        });
        compileGraph.nodes.push_back(Node {
            .typeId      = "project.tint_green",
            .id          = "tint",
            .displayName = "Project Tint Green",
            .params      = {{"amount", 0.4f}},
        });
        compileGraph.nodes.push_back(Node {
            .typeId      = "project.boost_color",
            .id          = "boost",
            .displayName = "Project Boost Color",
            .params      = {{"multiplier", 1.25f}},
        });
        compileGraph.nodes.push_back(Node {
            .typeId      = "vultra.output.surface",
            .id          = "out",
            .displayName = "Surface Output",
            .params      = {{"shadingModel", "Unlit"}},
        });
        compileGraph.links.push_back(Link {
            .from = {.nodeId = "color", .pin = "value"},
            .to   = {.nodeId = "tint", .pin = "color"},
        });
        compileGraph.links.push_back(Link {
            .from = {.nodeId = "tint", .pin = "out"},
            .to   = {.nodeId = "boost", .pin = "color"},
        });
        compileGraph.links.push_back(Link {
            .from = {.nodeId = "boost", .pin = "out"},
            .to   = {.nodeId = "out", .pin = "baseColor"},
        });

        const auto diagnostics = validateGraph(compileGraph, registry);
        require(!hasErrorFor(diagnostics, "tint"), "first project custom node in chain should validate");
        require(!hasErrorFor(diagnostics, "boost"), "second project custom node in chain should validate");

        MaterialGraphCompiler compiler {registry};
        SurfaceFunctionBackend backend;
        auto result = compiler.compile(
            CompileInput {
                           .graph    = compileGraph,
                           .shaderId = "custom/two-project-nodes",
                           .graphId  = stableGraphId("res://materials/two-project-nodes.vmatgraph.json"),
            },
            backend);
        require(result.has_value(), "graph with two project custom nodes should compile");
        require(result->vshaderSource.find("mix(") != std::string::npos,
                "first project custom node snippet should be emitted");
        require(result->vshaderSource.find("clamp(") != std::string::npos,
                "second project custom node snippet should be emitted");
        require(result->vshaderSource.find("vec4(0.0, 1.0, 0.0, 1.0)") != std::string::npos,
                "first project custom node literal should be preserved");
        require(result->vshaderSource.find("1.25") != std::string::npos,
                "second project custom node param override should be emitted");
    }

    {
        const auto parsed = loadNodeDescriptorFromText(R"json({
            "type": "MaterialGraphNode",
            "typeId": "vultra.custom.bad",
            "outputs": [{"name": "out", "type": "float"}]
        })json");
        require(!parsed.ok(), "custom node descriptor should reject reserved vultra namespace");
    }

    {
        const auto parsed = loadNodeDescriptorFromText(R"json({
            "type": "MaterialGraphNode",
            "typeId": "project.bad_impl",
            "outputs": [{"name": "out", "type": "float"}],
            "implementation": {
                "language": "glsl",
                "outputs": {"out": 42}
            }
        })json");
        require(!parsed.ok(), "custom node descriptor should reject non-string GLSL output expression");
    }

    {
        const auto parsed = loadNodeDescriptorFromText(R"json({
            "type": "MaterialGraphNode",
            "typeId": "project.bad_impl_output",
            "outputs": [{"name": "out", "type": "float"}],
            "implementation": {
                "language": "glsl",
                "outputs": {"missing": "{{param:value}}"}
            }
        })json");
        require(!parsed.ok(), "custom node descriptor should reject implementation outputs without declared pins");
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
