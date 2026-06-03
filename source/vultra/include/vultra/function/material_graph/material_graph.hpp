#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace vultra::material_graph
{
    enum class Domain : uint8_t
    {
        eSurface,
        ePostProcess,
    };

    enum class ValueType : uint8_t
    {
        eUnknown = 0,
        eBool,
        eInt,
        eFloat,
        eVec2,
        eVec3,
        eVec4,
        eColor,
        eTexture2D,
        eString,
    };

    enum class ShadingModel : uint8_t
    {
        ePBRMetallicRoughness,
        eUnlit,
        eToonLike,
        ePBRSpecularGlossiness,
        ePhong,
    };

    enum class AlphaMode : uint8_t
    {
        eOpaque,
        eMask,
        eBlend,
    };

    struct PinRef
    {
        std::string nodeId;
        std::string pin;
    };

    struct Link
    {
        PinRef from;
        PinRef to;
    };

    struct Pin
    {
        std::string              name;
        ValueType                type {ValueType::eUnknown};
        std::optional<nlohmann::json> defaultValue;
    };

    struct Node
    {
        std::string typeId;
        std::string id;
        std::string displayName;
        nlohmann::json params {nlohmann::json::object()};
        std::vector<Pin> inputs;
        std::vector<Pin> outputs;
        nlohmann::json editor {nlohmann::json::object()};
    };

    struct BlackboardParameter
    {
        std::string name;
        ValueType   type {ValueType::eFloat};
        nlohmann::json defaultValue;
        std::string displayName;
        float       uiMin {0.0f};
        float       uiMax {1.0f};
        bool        hasUiRange {false};
    };

    struct Graph
    {
        uint32_t version {1};
        Domain   domain {Domain::eSurface};
        std::string name;
        std::vector<Node> nodes;
        std::vector<Link> links;
        std::vector<BlackboardParameter> blackboard;
        nlohmann::json metadata {nlohmann::json::object()};
        nlohmann::json unknown {nlohmann::json::object()};
    };

    struct Diagnostic
    {
        enum class Severity : uint8_t
        {
            eInfo,
            eWarning,
            eError,
        };

        Severity severity {Severity::eError};
        std::string message;
        std::string nodeId;
        std::string pin;
    };

    [[nodiscard]] std::string_view toString(Domain domain);
    [[nodiscard]] std::string_view toString(ValueType type);
    [[nodiscard]] std::string_view toString(ShadingModel model);
    [[nodiscard]] std::string_view toString(AlphaMode mode);

    [[nodiscard]] Domain       domainFromString(std::string_view value);
    [[nodiscard]] ValueType    valueTypeFromString(std::string_view value);
    [[nodiscard]] ShadingModel shadingModelFromString(std::string_view value);
    [[nodiscard]] AlphaMode    alphaModeFromString(std::string_view value);

    [[nodiscard]] std::optional<Graph> graphFromJson(const nlohmann::json& json, std::vector<Diagnostic>* diagnostics = nullptr);
    [[nodiscard]] nlohmann::json       graphToJson(const Graph& graph);

    [[nodiscard]] std::optional<Graph> loadGraphFromText(std::string_view text, std::vector<Diagnostic>* diagnostics = nullptr);
    [[nodiscard]] std::string          saveGraphToText(const Graph& graph);

    [[nodiscard]] const Node* findNode(const Graph& graph, std::string_view nodeId);
    [[nodiscard]] const Link* findInputLink(const Graph& graph, std::string_view nodeId, std::string_view pin);
} // namespace vultra::material_graph
