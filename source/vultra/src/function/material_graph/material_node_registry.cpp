#include "vultra/function/material_graph/material_node_registry.hpp"

#include <algorithm>
#include <optional>
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

        NodeDescriptor desc(std::string      typeId,
                            std::string      displayName,
                            std::vector<Pin> inputs,
                            std::vector<Pin> outputs,
                            nlohmann::json   defaultParams = nlohmann::json::object())
        {
            return NodeDescriptor {
                .typeId        = std::move(typeId),
                .displayName   = std::move(displayName),
                .inputs        = std::move(inputs),
                .outputs       = std::move(outputs),
                .defaultParams = std::move(defaultParams),
            };
        }

        bool hasPin(const std::vector<Pin>& pins, const std::string& name)
        {
            return std::ranges::find_if(pins, [&](const Pin& pin) { return pin.name == name; }) != pins.end();
        }

        std::optional<Pin> pinFromJson(const nlohmann::json& json, std::vector<std::string>& diagnostics)
        {
            if (!json.is_object())
            {
                diagnostics.push_back("Material graph node pin must be an object.");
                return std::nullopt;
            }

            Pin pin;
            pin.name = json.value("name", std::string {});
            if (pin.name.empty())
                diagnostics.push_back("Material graph node pin requires name.");

            const auto typeText = json.value("type", std::string {});
            pin.type            = valueTypeFromString(typeText);
            if (typeText.empty())
                diagnostics.push_back("Material graph node pin '" + pin.name + "' requires type.");
            else if (pin.type == ValueType::eUnknown)
                diagnostics.push_back("Unknown material graph node pin type '" + typeText + "' for pin '" + pin.name + "'.");

            if (json.contains("defaultValue"))
                pin.defaultValue = json["defaultValue"];
            else if (json.contains("default"))
                pin.defaultValue = json["default"];

            if (pin.name.empty() || pin.type == ValueType::eUnknown)
                return std::nullopt;
            return pin;
        }

        std::vector<Pin> pinsFromJson(const nlohmann::json& json,
                                      const char*           fieldName,
                                      std::vector<std::string>& diagnostics)
        {
            std::vector<Pin> pins;
            if (!json.is_array())
            {
                diagnostics.push_back(std::string {"Material graph node "} + fieldName + " must be an array.");
                return pins;
            }
            std::unordered_set<std::string> names;
            for (const auto& item : json)
            {
                auto pin = pinFromJson(item, diagnostics);
                if (!pin)
                    continue;
                if (!names.insert(pin->name).second)
                {
                    diagnostics.push_back(std::string {"Duplicate material graph node pin name in "} + fieldName +
                                          ": " + pin->name + ".");
                    continue;
                }
                pins.push_back(std::move(*pin));
            }
            return pins;
        }
    } // namespace

    NodeRegistry makeBuiltinNodeRegistry()
    {
        NodeRegistry registry;
        auto         add = [&](NodeDescriptor descriptor) { (void)registry.registerNode(std::move(descriptor)); };

        add(desc("vultra.input.uv0", "UV0", {}, {pin("uv", ValueType::eVec2)}));
        add(desc("vultra.input.world_position", "World Position", {}, {pin("position", ValueType::eVec3)}));
        add(desc("vultra.input.world_normal", "World Normal", {}, {pin("normal", ValueType::eVec3)}));
        add(desc("vultra.input.view_direction", "View Direction", {}, {pin("direction", ValueType::eVec3)}));
        add(desc("vultra.input.view_index", "View Index", {}, {pin("index", ValueType::eInt)}));
        add(desc("vultra.input.eye_index", "Eye Index", {}, {pin("index", ValueType::eInt)}));
        add(desc("vultra.input.view_count", "View Count", {}, {pin("count", ValueType::eInt)}));
        add(desc("vultra.input.is_stereo_view", "Is Stereo View", {}, {pin("stereo", ValueType::eBool)}));
        add(desc("vultra.input.material_index", "Material Index", {}, {pin("index", ValueType::eInt)}));
        add(desc("vultra.input.time", "Time", {}, {pin("seconds", ValueType::eFloat)}));

        add(desc("vultra.param.float", "Float", {}, {pin("value", ValueType::eFloat)}, {{"value", 0.0f}}));
        add(desc("vultra.param.vec2", "Vec2", {}, {pin("value", ValueType::eVec2)}, {{"value", {0.0f, 0.0f}}}));
        add(desc("vultra.param.vec3", "Vec3", {}, {pin("value", ValueType::eVec3)}, {{"value", {0.0f, 0.0f, 0.0f}}}));
        add(desc(
            "vultra.param.vec4", "Vec4", {}, {pin("value", ValueType::eVec4)}, {{"value", {0.0f, 0.0f, 0.0f, 1.0f}}}));
        add(desc("vultra.param.color",
                 "Color",
                 {},
                 {pin("value", ValueType::eColor)},
                 {{"value", {1.0f, 1.0f, 1.0f, 1.0f}}}));
        add(desc("vultra.param.bool", "Bool", {}, {pin("value", ValueType::eBool)}, {{"value", false}}));
        add(desc("vultra.param.int", "Int", {}, {pin("value", ValueType::eInt)}, {{"value", 0}}));
        add(desc("vultra.param.enum", "Enum", {}, {pin("value", ValueType::eInt)}, {{"value", 0}}));
        add(desc(
            "vultra.param.texture2d", "Texture2D", {}, {pin("texture", ValueType::eTexture2D)}, {{"texture", ""}}));

        const auto number = std::vector {pin("a", ValueType::eFloat, 0.0f), pin("b", ValueType::eFloat, 0.0f)};
        add(desc("vultra.math.add", "Add", number, {pin("out", ValueType::eFloat)}));
        add(desc("vultra.math.subtract", "Subtract", number, {pin("out", ValueType::eFloat)}));
        add(desc("vultra.math.multiply", "Multiply", number, {pin("out", ValueType::eFloat)}));
        add(desc("vultra.math.divide", "Divide", number, {pin("out", ValueType::eFloat)}));
        add(desc("vultra.math.one_minus",
                 "One Minus",
                 {pin("v", ValueType::eFloat, 0.0f)},
                 {pin("out", ValueType::eFloat)}));
        add(desc("vultra.math.power",
                 "Power",
                 {pin("base", ValueType::eFloat, 1.0f), pin("exponent", ValueType::eFloat, 1.0f)},
                 {pin("out", ValueType::eFloat)}));
        add(desc("vultra.math.min", "Min", number, {pin("out", ValueType::eFloat)}));
        add(desc("vultra.math.max", "Max", number, {pin("out", ValueType::eFloat)}));
        add(desc("vultra.math.sine", "Sine", {pin("v", ValueType::eFloat, 0.0f)}, {pin("out", ValueType::eFloat)}));
        add(desc("vultra.math.fract", "Fract", {pin("v", ValueType::eFloat, 0.0f)}, {pin("out", ValueType::eFloat)}));
        add(desc("vultra.math.dot",
                 "Dot",
                 {pin("a", ValueType::eVec3, nlohmann::json::array({0.0f, 0.0f, 0.0f})),
                  pin("b", ValueType::eVec3, nlohmann::json::array({0.0f, 1.0f, 0.0f}))},
                 {pin("out", ValueType::eFloat)}));
        add(desc("vultra.math.normalize",
                 "Normalize",
                 {pin("v", ValueType::eVec3, nlohmann::json::array({0.0f, 1.0f, 0.0f}))},
                 {pin("out", ValueType::eVec3)}));
        add(desc("vultra.math.clamp",
                 "Clamp",
                 {pin("v", ValueType::eFloat, 0.0f),
                  pin("min", ValueType::eFloat, 0.0f),
                  pin("max", ValueType::eFloat, 1.0f)},
                 {pin("out", ValueType::eFloat)}));
        add(desc(
            "vultra.math.saturate", "Saturate", {pin("v", ValueType::eFloat, 0.0f)}, {pin("out", ValueType::eFloat)}));
        add(desc("vultra.math.smoothstep",
                 "Smoothstep",
                 {pin("edge0", ValueType::eFloat, 0.0f),
                  pin("edge1", ValueType::eFloat, 1.0f),
                  pin("x", ValueType::eFloat, 0.5f)},
                 {pin("out", ValueType::eFloat)}));
        add(desc("vultra.math.mix",
                 "Mix",
                 {pin("a", ValueType::eVec4, nlohmann::json::array({0.0f, 0.0f, 0.0f, 1.0f})),
                  pin("b", ValueType::eVec4, nlohmann::json::array({1.0f, 1.0f, 1.0f, 1.0f})),
                  pin("t", ValueType::eFloat, 0.5f)},
                 {pin("out", ValueType::eVec4)}));
        add(desc("vultra.vector.split_vec2",
                 "Split Vec2",
                 {pin("v", ValueType::eVec2, nlohmann::json::array({0.0f, 0.0f}))},
                 {pin("x", ValueType::eFloat), pin("y", ValueType::eFloat)}));

        add(desc("vultra.texture.sample2d",
                 "Sample Texture2D",
                 {pin("texture", ValueType::eTexture2D), pin("uv", ValueType::eVec2)},
                 {pin("rgba", ValueType::eVec4), pin("rgb", ValueType::eVec3), pin("a", ValueType::eFloat)}));
        add(desc("vultra.utility.normal_map",
                 "Normal Map",
                 {pin("sample", ValueType::eVec3), pin("normalWS", ValueType::eVec3)},
                 {pin("normal", ValueType::eVec3)}));
        add(desc(
            "vultra.utility.fresnel",
            "Fresnel",
            {pin("normalWS", ValueType::eVec3), pin("viewDirWS", ValueType::eVec3), pin("power", ValueType::eFloat)},
            {pin("factor", ValueType::eFloat)}));

        add(desc("vultra.output.surface",
                 "Surface Output",
                 {
                     pin("baseColor", ValueType::eColor, nlohmann::json::array({1.0f, 1.0f, 1.0f, 1.0f})),
                     pin("normal", ValueType::eVec3),
                     pin("metallic", ValueType::eFloat, 0.0f),
                     pin("roughness", ValueType::eFloat, 1.0f),
                     pin("ao", ValueType::eFloat, 1.0f),
                     pin("emissive", ValueType::eVec3, nlohmann::json::array({0.0f, 0.0f, 0.0f})),
                     pin("alpha", ValueType::eFloat, 1.0f),
                     pin("alphaCutoff", ValueType::eFloat, 0.5f),
                 },
                 {},
                 {
                     {"baseColor", {1.0f, 1.0f, 1.0f, 1.0f}},
                     {"metallic", 0.0f},
                     {"roughness", 1.0f},
                     {"ao", 1.0f},
                     {"emissive", {0.0f, 0.0f, 0.0f}},
                     {"alpha", 1.0f},
                     {"alphaCutoff", 0.5f},
                     {"shadingModel", "PBR_MR"},
                     {"alphaMode", "Opaque"},
                 }));

        return registry;
    }

    NodeDescriptorParseResult nodeDescriptorFromJson(const nlohmann::json& root)
    {
        NodeDescriptorParseResult result;
        auto&                     desc = result.descriptor;

        if (!root.is_object())
        {
            result.diagnostics.push_back("Material graph node descriptor root must be an object.");
            return result;
        }

        const auto type = root.value("type", std::string {"MaterialGraphNode"});
        if (type != "MaterialGraphNode")
            result.diagnostics.push_back("Material graph node descriptor type must be \"MaterialGraphNode\".");

        if (root.contains("version") && !root["version"].is_number_integer() && !root["version"].is_number_unsigned())
            result.diagnostics.push_back("Material graph node descriptor version must be an integer.");

        desc.typeId = root.value("typeId", root.value("id", std::string {}));
        if (desc.typeId.empty())
            result.diagnostics.push_back("Material graph node descriptor requires typeId.");
        if (desc.typeId.starts_with("vultra."))
            result.diagnostics.push_back("Custom material graph node typeId must not use the reserved vultra.* namespace.");

        desc.displayName = root.value("displayName", root.value("name", std::string {}));
        if (desc.displayName.empty())
            desc.displayName = desc.typeId;

        if (root.contains("inputs"))
            desc.inputs = pinsFromJson(root["inputs"], "inputs", result.diagnostics);
        if (root.contains("outputs"))
            desc.outputs = pinsFromJson(root["outputs"], "outputs", result.diagnostics);
        if (desc.outputs.empty())
            result.diagnostics.push_back("Material graph node descriptor requires at least one output pin.");

        if (root.contains("defaultParams"))
        {
            if (!root["defaultParams"].is_object())
                result.diagnostics.push_back("Material graph node descriptor defaultParams must be an object.");
            else
                desc.defaultParams = root["defaultParams"];
        }
        else if (root.contains("params"))
        {
            if (!root["params"].is_object())
                result.diagnostics.push_back("Material graph node descriptor params must be an object.");
            else
                desc.defaultParams = root["params"];
        }

        if (root.contains("implementation"))
        {
            if (!root["implementation"].is_object())
            {
                result.diagnostics.push_back("Material graph node descriptor implementation must be an object.");
            }
            else
            {
                desc.implementation = root["implementation"];
                const auto language = desc.implementation.value("language", std::string {"glsl"});
                if (language != "glsl")
                    result.diagnostics.push_back("Material graph node descriptor implementation.language must be \"glsl\".");
                if (desc.implementation.contains("outputs"))
                {
                    if (!desc.implementation["outputs"].is_object())
                    {
                        result.diagnostics.push_back(
                            "Material graph node descriptor implementation.outputs must be an object.");
                    }
                    else
                    {
                        for (const auto& [name, value] : desc.implementation["outputs"].items())
                        {
                            if (!hasPin(desc.outputs, name))
                                result.diagnostics.push_back(
                                    "Material graph node descriptor implementation output '" + name +
                                    "' does not match any declared output pin.");
                            if (!value.is_string())
                                result.diagnostics.push_back(
                                    "Material graph node descriptor implementation output '" + name +
                                    "' must be a string expression.");
                        }
                    }
                }
            }
        }

        return result;
    }

    NodeDescriptorParseResult loadNodeDescriptorFromText(const std::string_view text)
    {
        try
        {
            return nodeDescriptorFromJson(nlohmann::json::parse(text));
        }
        catch (const nlohmann::json::exception& e)
        {
            NodeDescriptorParseResult result;
            result.diagnostics.push_back(std::string {"Failed to parse material graph node descriptor JSON: "} + e.what());
            return result;
        }
    }

    std::vector<Diagnostic> validateGraph(const Graph& graph, const NodeRegistry& registry)
    {
        std::vector<Diagnostic>         diagnostics;
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
                diagnostics.push_back(
                    {.message = "Unknown material graph node type: " + node.typeId, .nodeId = node.id});
                continue;
            }

            for (const auto& input : node.inputs)
            {
                if (!hasPin(nodeDesc->inputs, input.name))
                    diagnostics.push_back({.severity = Diagnostic::Severity::eWarning,
                                           .message  = "Node has undeclared input pin",
                                           .nodeId   = node.id,
                                           .pin      = input.name});
            }
            for (const auto& output : node.outputs)
            {
                if (!hasPin(nodeDesc->outputs, output.name))
                    diagnostics.push_back({.severity = Diagnostic::Severity::eWarning,
                                           .message  = "Node has undeclared output pin",
                                           .nodeId   = node.id,
                                           .pin      = output.name});
            }
        }

        for (const auto& link : graph.links)
        {
            const auto* from = findNode(graph, link.from.nodeId);
            const auto* to   = findNode(graph, link.to.nodeId);
            if (!from)
                diagnostics.push_back({.message = "Link references missing source node",
                                       .nodeId  = link.from.nodeId,
                                       .pin     = link.from.pin});
            if (!to)
                diagnostics.push_back(
                    {.message = "Link references missing target node", .nodeId = link.to.nodeId, .pin = link.to.pin});
        }

        const bool hasSurfaceOutput =
            std::ranges::any_of(graph.nodes, [](const Node& node) { return node.typeId == "vultra.output.surface"; });
        if (graph.domain == Domain::eSurface && !hasSurfaceOutput)
            diagnostics.push_back({.message = "Surface material graph requires a Surface Output node"});

        return diagnostics;
    }
} // namespace vultra::material_graph
