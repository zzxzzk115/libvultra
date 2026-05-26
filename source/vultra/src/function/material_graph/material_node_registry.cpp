#include "vultra/function/material_graph/material_node_registry.hpp"

#include <algorithm>
#include <unordered_set>

namespace vultra::material_graph
{
    bool NodeRegistry::registerNode(NodeDescriptor descriptor)
    {
        if (descriptor.typeId.empty() || m_Descriptors.contains(descriptor.typeId))
            return false;
        m_Descriptors.emplace(descriptor.typeId, std::move(descriptor));
        return true;
    }

    const NodeDescriptor* NodeRegistry::find(const std::string_view typeId) const
    {
        const auto it = m_Descriptors.find(std::string(typeId));
        return it == m_Descriptors.end() ? nullptr : &it->second;
    }

    bool NodeRegistry::contains(const std::string_view typeId) const { return find(typeId) != nullptr; }

    std::vector<std::string> NodeRegistry::typeIds() const
    {
        std::vector<std::string> ids;
        ids.reserve(m_Descriptors.size());
        for (const auto& [id, _] : m_Descriptors)
        {
            static_cast<void>(_);
            ids.push_back(id);
        }
        std::ranges::sort(ids);
        return ids;
    }

    namespace
    {
        Pin pin(std::string name, ValueType type, std::optional<nlohmann::json> defaultValue = std::nullopt)
        {
            return Pin {.name = std::move(name), .type = type, .defaultValue = std::move(defaultValue)};
        }

        NodeDescriptor desc(std::string typeId,
                            std::string displayName,
                            std::vector<Pin> inputs,
                            std::vector<Pin> outputs,
                            nlohmann::json defaultParams = nlohmann::json::object())
        {
            return NodeDescriptor {
                .typeId = std::move(typeId),
                .displayName = std::move(displayName),
                .inputs = std::move(inputs),
                .outputs = std::move(outputs),
                .defaultParams = std::move(defaultParams),
            };
        }

        bool hasPin(const std::vector<Pin>& pins, const std::string& name)
        {
            return std::ranges::find_if(pins, [&](const Pin& pin) { return pin.name == name; }) != pins.end();
        }
    } // namespace

    NodeRegistry makeBuiltinNodeRegistry()
    {
        NodeRegistry registry;
        auto add = [&](NodeDescriptor descriptor) {
            (void)registry.registerNode(std::move(descriptor));
        };

        add(desc("vultra.input.uv0", "UV0", {}, {pin("uv", ValueType::eVec2)}));
        add(desc("vultra.input.world_position", "World Position", {}, {pin("position", ValueType::eVec3)}));
        add(desc("vultra.input.world_normal", "World Normal", {}, {pin("normal", ValueType::eVec3)}));
        add(desc("vultra.input.view_direction", "View Direction", {}, {pin("direction", ValueType::eVec3)}));
        add(desc("vultra.input.material_index", "Material Index", {}, {pin("index", ValueType::eInt)}));

        add(desc("vultra.param.float", "Float", {}, {pin("value", ValueType::eFloat)}, {{"value", 0.0f}}));
        add(desc("vultra.param.vec2", "Vec2", {}, {pin("value", ValueType::eVec2)}, {{"value", {0.0f, 0.0f}}}));
        add(desc("vultra.param.vec3", "Vec3", {}, {pin("value", ValueType::eVec3)}, {{"value", {0.0f, 0.0f, 0.0f}}}));
        add(desc("vultra.param.vec4", "Vec4", {}, {pin("value", ValueType::eVec4)}, {{"value", {0.0f, 0.0f, 0.0f, 1.0f}}}));
        add(desc("vultra.param.color", "Color", {}, {pin("value", ValueType::eColor)}, {{"value", {1.0f, 1.0f, 1.0f, 1.0f}}}));
        add(desc("vultra.param.bool", "Bool", {}, {pin("value", ValueType::eBool)}, {{"value", false}}));
        add(desc("vultra.param.int", "Int", {}, {pin("value", ValueType::eInt)}, {{"value", 0}}));
        add(desc("vultra.param.texture2d", "Texture2D", {}, {pin("texture", ValueType::eTexture2D)}, {{"texture", ""}}));

        const auto number = std::vector {pin("a", ValueType::eFloat), pin("b", ValueType::eFloat)};
        add(desc("vultra.math.add", "Add", number, {pin("out", ValueType::eFloat)}));
        add(desc("vultra.math.subtract", "Subtract", number, {pin("out", ValueType::eFloat)}));
        add(desc("vultra.math.multiply", "Multiply", number, {pin("out", ValueType::eFloat)}));
        add(desc("vultra.math.divide", "Divide", number, {pin("out", ValueType::eFloat)}));
        add(desc("vultra.math.dot", "Dot", {pin("a", ValueType::eVec3), pin("b", ValueType::eVec3)}, {pin("out", ValueType::eFloat)}));
        add(desc("vultra.math.normalize", "Normalize", {pin("v", ValueType::eVec3)}, {pin("out", ValueType::eVec3)}));
        add(desc("vultra.math.clamp", "Clamp", {pin("v", ValueType::eFloat), pin("min", ValueType::eFloat), pin("max", ValueType::eFloat)}, {pin("out", ValueType::eFloat)}));
        add(desc("vultra.math.saturate", "Saturate", {pin("v", ValueType::eFloat)}, {pin("out", ValueType::eFloat)}));
        add(desc("vultra.math.mix", "Mix", {pin("a", ValueType::eVec4), pin("b", ValueType::eVec4), pin("t", ValueType::eFloat)}, {pin("out", ValueType::eVec4)}));

        add(desc("vultra.texture.sample2d", "Sample Texture2D", {pin("texture", ValueType::eTexture2D), pin("uv", ValueType::eVec2)}, {pin("rgba", ValueType::eVec4), pin("rgb", ValueType::eVec3), pin("a", ValueType::eFloat)}));
        add(desc("vultra.utility.normal_map", "Normal Map", {pin("sample", ValueType::eVec3), pin("normalWS", ValueType::eVec3)}, {pin("normal", ValueType::eVec3)}));
        add(desc("vultra.utility.fresnel", "Fresnel", {pin("normalWS", ValueType::eVec3), pin("viewDirWS", ValueType::eVec3), pin("power", ValueType::eFloat)}, {pin("factor", ValueType::eFloat)}));

        add(desc("vultra.output.surface",
                                   "Surface Output",
                                   {
                                       pin("baseColor", ValueType::eVec4, nlohmann::json::array({1.0f, 1.0f, 1.0f, 1.0f})),
                                       pin("normal", ValueType::eVec3),
                                       pin("metallic", ValueType::eFloat, 0.0f),
                                       pin("roughness", ValueType::eFloat, 1.0f),
                                       pin("ao", ValueType::eFloat, 1.0f),
                                       pin("emissive", ValueType::eVec3, nlohmann::json::array({0.0f, 0.0f, 0.0f})),
                                       pin("alpha", ValueType::eFloat, 1.0f),
                                       pin("alphaCutoff", ValueType::eFloat, 0.5f),
                                   },
                                   {},
                                   {{"shadingModel", "Lit"}, {"alphaMode", "Opaque"}}));

        return registry;
    }

    std::vector<Diagnostic> validateGraph(const Graph& graph, const NodeRegistry& registry)
    {
        std::vector<Diagnostic> diagnostics;
        std::unordered_set<std::string> nodeIds;

        for (const auto& node : graph.nodes)
        {
            if (!nodeIds.insert(node.id).second)
            {
                diagnostics.push_back({.message = "Duplicate material graph node id", .nodeId = node.id});
            }

            const auto* nodeDesc = registry.find(node.typeId);
            if (!nodeDesc)
            {
                diagnostics.push_back({.message = "Unknown material graph node type: " + node.typeId, .nodeId = node.id});
                continue;
            }

            for (const auto& input : node.inputs)
            {
                if (!hasPin(nodeDesc->inputs, input.name))
                    diagnostics.push_back({.severity = Diagnostic::Severity::eWarning, .message = "Node has undeclared input pin", .nodeId = node.id, .pin = input.name});
            }
            for (const auto& output : node.outputs)
            {
                if (!hasPin(nodeDesc->outputs, output.name))
                    diagnostics.push_back({.severity = Diagnostic::Severity::eWarning, .message = "Node has undeclared output pin", .nodeId = node.id, .pin = output.name});
            }
        }

        for (const auto& link : graph.links)
        {
            const auto* from = findNode(graph, link.from.nodeId);
            const auto* to   = findNode(graph, link.to.nodeId);
            if (!from)
                diagnostics.push_back({.message = "Link references missing source node", .nodeId = link.from.nodeId, .pin = link.from.pin});
            if (!to)
                diagnostics.push_back({.message = "Link references missing target node", .nodeId = link.to.nodeId, .pin = link.to.pin});
        }

        const bool hasSurfaceOutput = std::ranges::any_of(graph.nodes, [](const Node& node) {
            return node.typeId == "vultra.output.surface";
        });
        if (graph.domain == Domain::eSurface && !hasSurfaceOutput)
            diagnostics.push_back({.message = "Surface material graph requires a Surface Output node"});

        return diagnostics;
    }
} // namespace vultra::material_graph
