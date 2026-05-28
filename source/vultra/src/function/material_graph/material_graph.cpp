#include "vultra/function/material_graph/material_graph.hpp"

#include <array>
#include <sstream>

namespace vultra::material_graph
{
    namespace
    {
        void addDiag(std::vector<Diagnostic>* diagnostics,
                     Diagnostic::Severity     severity,
                     std::string              message,
                     std::string              nodeId = {},
                     std::string              pin    = {})
        {
            if (!diagnostics)
                return;
            diagnostics->push_back(Diagnostic {
                .severity = severity,
                .message  = std::move(message),
                .nodeId   = std::move(nodeId),
                .pin      = std::move(pin),
            });
        }

        std::optional<Pin> pinFromJson(const nlohmann::json& json)
        {
            if (!json.is_object())
                return std::nullopt;
            Pin pin;
            pin.name = json.value("name", std::string {});
            pin.type = valueTypeFromString(json.value("type", std::string {"unknown"}));
            if (json.contains("default"))
                pin.defaultValue = json.at("default");
            if (pin.name.empty())
                return std::nullopt;
            return pin;
        }

        nlohmann::json pinToJson(const Pin& pin)
        {
            nlohmann::json json {
                {"name", pin.name},
                {"type", std::string(toString(pin.type))},
            };
            if (pin.defaultValue)
                json["default"] = *pin.defaultValue;
            return json;
        }

        std::vector<Pin> pinsFromJson(const nlohmann::json& json)
        {
            std::vector<Pin> pins;
            if (!json.is_array())
                return pins;
            for (const auto& item : json)
                if (auto pin = pinFromJson(item))
                    pins.push_back(std::move(*pin));
            return pins;
        }

        nlohmann::json pinsToJson(const std::vector<Pin>& pins)
        {
            auto json = nlohmann::json::array();
            for (const auto& pin : pins)
                json.push_back(pinToJson(pin));
            return json;
        }

        PinRef pinRefFromJson(const nlohmann::json& json)
        {
            if (json.is_object())
                return PinRef {.nodeId = json.value("node", std::string {}), .pin = json.value("pin", std::string {})};
            return {};
        }

        nlohmann::json pinRefToJson(const PinRef& ref)
        {
            return nlohmann::json {
                {"node", ref.nodeId},
                {"pin", ref.pin},
            };
        }
    } // namespace

    std::string_view toString(const Domain domain)
    {
        switch (domain)
        {
            case Domain::ePostProcess:
                return "postprocess";
            case Domain::eSurface:
            default:
                return "surface";
        }
    }

    std::string_view toString(const ValueType type)
    {
        switch (type)
        {
            case ValueType::eBool:
                return "bool";
            case ValueType::eInt:
                return "int";
            case ValueType::eFloat:
                return "float";
            case ValueType::eVec2:
                return "vec2";
            case ValueType::eVec3:
                return "vec3";
            case ValueType::eVec4:
                return "vec4";
            case ValueType::eColor:
                return "color";
            case ValueType::eTexture2D:
                return "texture2D";
            case ValueType::eString:
                return "string";
            case ValueType::eUnknown:
            default:
                return "unknown";
        }
    }

    std::string_view toString(const ShadingModel model)
    {
        switch (model)
        {
            case ShadingModel::eUnlit:
                return "Unlit";
            case ShadingModel::eToonLike:
                return "ToonLike";
            case ShadingModel::ePBRSpecularGlossiness:
                return "PBR_SpecGloss";
            case ShadingModel::ePhong:
                return "Phong";
            case ShadingModel::ePBRMetallicRoughness:
            default:
                return "PBR_MR";
        }
    }

    std::string_view toString(const AlphaMode mode)
    {
        switch (mode)
        {
            case AlphaMode::eMask:
                return "Mask";
            case AlphaMode::eBlend:
                return "Blend";
            case AlphaMode::eOpaque:
            default:
                return "Opaque";
        }
    }

    Domain domainFromString(const std::string_view value)
    {
        if (value == "postprocess" || value == "post_process")
            return Domain::ePostProcess;
        return Domain::eSurface;
    }

    ValueType valueTypeFromString(const std::string_view value)
    {
        if (value == "bool")
            return ValueType::eBool;
        if (value == "int")
            return ValueType::eInt;
        if (value == "float")
            return ValueType::eFloat;
        if (value == "vec2")
            return ValueType::eVec2;
        if (value == "vec3")
            return ValueType::eVec3;
        if (value == "vec4")
            return ValueType::eVec4;
        if (value == "color")
            return ValueType::eColor;
        if (value == "texture2D" || value == "texture_2d")
            return ValueType::eTexture2D;
        if (value == "string")
            return ValueType::eString;
        return ValueType::eUnknown;
    }

    ShadingModel shadingModelFromString(const std::string_view value)
    {
        if (value == "Unlit" || value == "unlit")
            return ShadingModel::eUnlit;
        if (value == "ToonLike" || value == "toon_like" || value == "toon")
            return ShadingModel::eToonLike;
        if (value == "PBR_SpecGloss" || value == "pbr_spec_gloss" || value == "PBRSpecularGlossiness" ||
            value == "specular_glossiness" || value == "SpecularGlossiness")
            return ShadingModel::ePBRSpecularGlossiness;
        if (value == "Phong" || value == "phong")
            return ShadingModel::ePhong;
        return ShadingModel::ePBRMetallicRoughness;
    }

    AlphaMode alphaModeFromString(const std::string_view value)
    {
        if (value == "Mask" || value == "mask")
            return AlphaMode::eMask;
        if (value == "Blend" || value == "blend")
            return AlphaMode::eBlend;
        return AlphaMode::eOpaque;
    }

    std::optional<Graph> graphFromJson(const nlohmann::json& json, std::vector<Diagnostic>* diagnostics)
    {
        if (!json.is_object())
        {
            addDiag(diagnostics, Diagnostic::Severity::eError, "Material graph root must be an object");
            return std::nullopt;
        }

        Graph graph;
        graph.version  = json.value("version", 1u);
        graph.domain   = domainFromString(json.value("domain", std::string {"surface"}));
        graph.name     = json.value("name", std::string {});
        graph.metadata = json.value("metadata", nlohmann::json::object());

        static constexpr std::array knownKeys {
            "version",
            "domain",
            "name",
            "nodes",
            "links",
            "metadata",
        };
        for (const auto& [key, value] : json.items())
        {
            bool known = false;
            for (const auto* knownKey : knownKeys)
                known = known || key == knownKey;
            if (!known)
                graph.unknown[key] = value;
        }

        const auto nodes = json.value("nodes", nlohmann::json::array());
        if (!nodes.is_array())
        {
            addDiag(diagnostics, Diagnostic::Severity::eError, "Material graph nodes must be an array");
            return std::nullopt;
        }

        for (const auto& item : nodes)
        {
            if (!item.is_object())
            {
                addDiag(diagnostics, Diagnostic::Severity::eWarning, "Skipped non-object material graph node");
                continue;
            }
            Node node;
            node.typeId      = item.value("type", std::string {});
            node.id          = item.value("id", std::string {});
            node.displayName = item.value("displayName", std::string {});
            node.params      = item.value("params", nlohmann::json::object());
            node.inputs      = pinsFromJson(item.value("inputs", nlohmann::json::array()));
            node.outputs     = pinsFromJson(item.value("outputs", nlohmann::json::array()));
            node.editor      = item.value("editor", nlohmann::json::object());
            if (node.id.empty() || node.typeId.empty())
            {
                addDiag(diagnostics, Diagnostic::Severity::eWarning, "Skipped node with missing id or type");
                continue;
            }
            graph.nodes.push_back(std::move(node));
        }

        const auto links = json.value("links", nlohmann::json::array());
        if (!links.is_array())
        {
            addDiag(diagnostics, Diagnostic::Severity::eError, "Material graph links must be an array");
            return std::nullopt;
        }
        for (const auto& item : links)
        {
            if (!item.is_object())
                continue;
            Link link;
            link.from = pinRefFromJson(item.value("from", nlohmann::json::object()));
            link.to   = pinRefFromJson(item.value("to", nlohmann::json::object()));
            if (!link.from.nodeId.empty() && !link.to.nodeId.empty() && !link.from.pin.empty() && !link.to.pin.empty())
                graph.links.push_back(std::move(link));
        }

        return graph;
    }

    nlohmann::json graphToJson(const Graph& graph)
    {
        nlohmann::json json = graph.unknown.is_object() ? graph.unknown : nlohmann::json::object();
        json["version"]     = graph.version;
        json["domain"]      = std::string(toString(graph.domain));
        json["name"]        = graph.name;
        json["metadata"]    = graph.metadata;

        json["nodes"] = nlohmann::json::array();
        for (const auto& node : graph.nodes)
        {
            json["nodes"].push_back(nlohmann::json {
                {"type", node.typeId},
                {"id", node.id},
                {"displayName", node.displayName},
                {"params", node.params},
                {"inputs", pinsToJson(node.inputs)},
                {"outputs", pinsToJson(node.outputs)},
                {"editor", node.editor},
            });
        }

        json["links"] = nlohmann::json::array();
        for (const auto& link : graph.links)
            json["links"].push_back(nlohmann::json {{"from", pinRefToJson(link.from)}, {"to", pinRefToJson(link.to)}});
        return json;
    }

    std::optional<Graph> loadGraphFromText(const std::string_view text, std::vector<Diagnostic>* diagnostics)
    {
        try
        {
            return graphFromJson(nlohmann::json::parse(text), diagnostics);
        }
        catch (const std::exception& e)
        {
            addDiag(diagnostics,
                    Diagnostic::Severity::eError,
                    std::string("Material graph JSON parse failed: ") + e.what());
            return std::nullopt;
        }
    }

    std::string saveGraphToText(const Graph& graph) { return graphToJson(graph).dump(4); }

    const Node* findNode(const Graph& graph, const std::string_view nodeId)
    {
        for (const auto& node : graph.nodes)
            if (node.id == nodeId)
                return &node;
        return nullptr;
    }

    const Link* findInputLink(const Graph& graph, const std::string_view nodeId, const std::string_view pin)
    {
        for (const auto& link : graph.links)
            if (link.to.nodeId == nodeId && link.to.pin == pin)
                return &link;
        return nullptr;
    }
} // namespace vultra::material_graph
