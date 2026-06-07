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
        // A custom node that references a whole helper/BXDF function by name via
        // implementation.includes (not inlined). The compiler must emit the
        // #include and the call into the generated surface source.
        const auto nodeText = R"json({
            "type": "MaterialGraphNode",
            "version": 1,
            "typeId": "project.bxdf.sheen",
            "displayName": "Sheen Rim",
            "inputs": [
                {"name": "tint", "type": "color", "defaultValue": [1.0, 1.0, 1.0, 1.0]}
            ],
            "outputs": [
                {"name": "rgb", "type": "vec3"}
            ],
            "implementation": {
                "language": "glsl",
                "includes": ["bxdf/sheen.glsl"],
                "outputs": {
                    "rgb": "vultra_node_sheen({{input:tint}}.rgb, vec3(0.0, 1.0, 0.0), vec3(0.0, 0.0, 1.0), 1.0)"
                }
            }
        })json";
        auto parsed = loadNodeDescriptorFromText(nodeText);
        require(parsed.ok(), "custom node with includes should parse");

        auto registry = makeBuiltinNodeRegistry();
        require(registry.registerNode(std::move(parsed.descriptor)), "sheen node should register");

        Graph compileGraph;
        compileGraph.version = 1;
        compileGraph.domain  = Domain::eSurface;
        compileGraph.nodes.push_back(Node {
            .typeId      = "vultra.param.color",
            .id          = "tint",
            .displayName = "Tint",
            .params      = {{"value", {1.0f, 1.0f, 1.0f, 1.0f}}},
        });
        compileGraph.nodes.push_back(Node {
            .typeId = "project.bxdf.sheen", .id = "sheen", .displayName = "Sheen Rim"});
        compileGraph.nodes.push_back(Node {
            .typeId      = "vultra.output.surface",
            .id          = "out",
            .displayName = "Surface Output",
            .params      = {{"shadingModel", "Unlit"}},
        });
        compileGraph.links.push_back(Link {.from = {.nodeId = "tint", .pin = "value"},
                                           .to   = {.nodeId = "sheen", .pin = "tint"}});
        compileGraph.links.push_back(Link {.from = {.nodeId = "sheen", .pin = "rgb"},
                                           .to   = {.nodeId = "out", .pin = "emissive"}});

        MaterialGraphCompiler  compiler {registry};
        SurfaceFunctionBackend backend;
        auto                   result = compiler.compile(
            CompileInput {.graph    = compileGraph,
                                            .shaderId = "custom/sheen",
                                            .graphId  = stableGraphId("res://materials/custom-sheen.vmatgraph.json")},
            backend);
        require(result.has_value(), "sheen node graph should compile");
        require(result->vshaderSource.find("#include \"bxdf/sheen.glsl\"") != std::string::npos,
                "node implementation.includes should emit an #include directive");
        require(result->vshaderSource.find("vultra_node_sheen(") != std::string::npos,
                "custom node should call the included helper function");
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

    {
        // MeshMaterialBackend wraps the surface eval into a full mesh-material
        // fragment (.vshader) that writes the GBuffer via VULTRA_MATERIAL_MAIN, so
        // a graph can render through the eShaderMaterial path (per-pixel GLSL).
        MaterialGraphCompiler compiler;
        MeshMaterialBackend   backend;
        auto                  result = compiler.compile(
            CompileInput {.graph    = makeColorOnlyGraph(),
                                           .shaderId = "folder/color-only.vmatgraph",
                                           .graphId  = stableGraphId("res://materials/color-only.vmatgraph")},
            backend);
        require(result.has_value(), "mesh-material backend should compile a color-only graph");
        require(result->vshaderSource.find("#include \"include/vultra/mesh_material.glsl\"") != std::string::npos,
                "mesh-material fragment should include the mesh-material ABI");
        require(result->vshaderSource.find("VULTRA_MATERIAL_MAIN(vultraGraphMaterial)") != std::string::npos,
                "mesh-material fragment should hook the GBuffer pass via VULTRA_MATERIAL_MAIN");
        require(result->vshaderSource.find("eval_material_graph_folder_color_only_vmatgraph(0u,") != std::string::npos,
                "wrapper should call the graph eval function");
        require(result->vshaderSource.find("OUT.shadingModel = vultra_graph_to_gbuffer_model(") != std::string::npos,
                "wrapper should remap the graph shading model to a GBuffer model code");
    }

    {
        // Demo parity: a graph driving TWO project custom nodes (duotone -> baseColor,
        // sheen -> emissive), each calling a helper FUNCTION pulled in by name. Mirrors
        // resources/materials/default.vmatgraph.json + nodes/{duotone,sheen}.vmatnode.json.
        const auto duotoneText = R"json({
            "type": "MaterialGraphNode", "version": 1,
            "typeId": "project.color.duotone", "displayName": "Duotone Gradient",
            "inputs": [
                {"name": "colorA", "type": "color", "default": [0.0, 0.0, 1.0, 1.0]},
                {"name": "colorB", "type": "color", "default": [1.0, 0.0, 0.0, 1.0]},
                {"name": "t", "type": "float", "default": 0.5}
            ],
            "outputs": [ {"name": "out", "type": "color"} ],
            "implementation": {
                "language": "glsl",
                "includes": ["color/duotone.glsl"],
                "outputs": { "out": "vultra_node_duotone({{input:colorA}}, {{input:colorB}}, {{input:t}})" }
            }
        })json";
        const auto sheenText = R"json({
            "type": "MaterialGraphNode", "version": 1,
            "typeId": "project.bxdf.sheen", "displayName": "Sheen Rim",
            "inputs": [
                {"name": "tint", "type": "color", "default": [1.0, 1.0, 1.0, 1.0]},
                {"name": "normalWS", "type": "vec3", "default": [0.0, 1.0, 0.0]},
                {"name": "viewDirWS", "type": "vec3", "default": [0.0, 0.0, 1.0]},
                {"name": "intensity", "type": "float", "default": 1.0}
            ],
            "outputs": [ {"name": "rgb", "type": "vec3"} ],
            "implementation": {
                "language": "glsl",
                "includes": ["bxdf/sheen.glsl"],
                "outputs": { "rgb": "vultra_node_sheen({{input:tint}}.rgb, {{input:normalWS}}, {{input:viewDirWS}}, {{input:intensity}})" }
            }
        })json";
        auto duotoneParsed = loadNodeDescriptorFromText(duotoneText);
        auto sheenParsed   = loadNodeDescriptorFromText(sheenText);
        require(duotoneParsed.ok(), "duotone custom node should parse");
        require(sheenParsed.ok(), "sheen custom node should parse");

        auto registry = makeBuiltinNodeRegistry();
        require(registry.registerNode(std::move(duotoneParsed.descriptor)), "duotone should register");
        require(registry.registerNode(std::move(sheenParsed.descriptor)), "sheen should register");

        Graph graph;
        graph.version = 1;
        graph.domain  = Domain::eSurface;
        graph.nodes.push_back(Node {.typeId = "vultra.param.color", .id = "cold",
                                    .params = {{"value", {0.02f, 0.16f, 1.0f, 1.0f}}}});
        graph.nodes.push_back(Node {.typeId = "vultra.param.color", .id = "hot",
                                    .params = {{"value", {1.0f, 0.08f, 0.0f, 1.0f}}}});
        graph.nodes.push_back(Node {.typeId = "project.color.duotone", .id = "duo"});
        graph.nodes.push_back(Node {.typeId = "vultra.param.color", .id = "sheenTint",
                                    .params = {{"value", {0.6f, 0.8f, 1.0f, 1.0f}}}});
        graph.nodes.push_back(Node {.typeId = "project.bxdf.sheen", .id = "sheen"});
        graph.nodes.push_back(Node {.typeId = "vultra.output.surface", .id = "out",
                                    .params = {{"shadingModel", "PBR_MR"}}});
        graph.links.push_back(Link {.from = {.nodeId = "cold", .pin = "value"}, .to = {.nodeId = "duo", .pin = "colorA"}});
        graph.links.push_back(Link {.from = {.nodeId = "hot", .pin = "value"}, .to = {.nodeId = "duo", .pin = "colorB"}});
        graph.links.push_back(Link {.from = {.nodeId = "duo", .pin = "out"}, .to = {.nodeId = "out", .pin = "baseColor"}});
        graph.links.push_back(Link {.from = {.nodeId = "sheenTint", .pin = "value"}, .to = {.nodeId = "sheen", .pin = "tint"}});
        graph.links.push_back(Link {.from = {.nodeId = "sheen", .pin = "rgb"}, .to = {.nodeId = "out", .pin = "emissive"}});

        const auto diagnostics = validateGraph(graph, registry);
        require(!hasErrorFor(diagnostics, "duo"), "duotone node should validate");
        require(!hasErrorFor(diagnostics, "sheen"), "sheen node should validate");

        MaterialGraphCompiler compiler {registry};
        MeshMaterialBackend   backend;
        auto                  result = compiler.compile(
            CompileInput {.graph    = graph,
                                           .shaderId = "default.vmatgraph",
                                           .graphId  = stableGraphId("res://materials/default.vmatgraph.json")},
            backend);
        require(result.has_value(), "two-custom-node demo graph should compile");
        const auto& src = result->vshaderSource;
        require(src.find("#include \"color/duotone.glsl\"") != std::string::npos, "duotone include emitted");
        require(src.find("#include \"bxdf/sheen.glsl\"") != std::string::npos, "sheen include emitted");
        require(src.find("vultra_node_duotone(") != std::string::npos, "duotone helper call emitted");
        require(src.find("vultra_node_sheen(") != std::string::npos, "sheen helper call emitted");
        require(src.find("VULTRA_MATERIAL_MAIN(vultraGraphMaterial)") != std::string::npos,
                "mesh-material wrapper emitted for the demo graph");
    }

    std::cout << "material_graph tests passed\n";
    return 0;
}
