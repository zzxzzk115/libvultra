#pragma once

#include "vultra/function/material_graph/material_graph.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace vultra::material_graph
{
    struct NodeDescriptor
    {
        std::string typeId;
        std::string displayName;
        std::vector<Pin> inputs;
        std::vector<Pin> outputs;
        nlohmann::json defaultParams {nlohmann::json::object()};
        nlohmann::json implementation {nlohmann::json::object()};
    };

    struct NodeDescriptorParseResult
    {
        NodeDescriptor descriptor;
        std::vector<std::string> diagnostics;

        [[nodiscard]] bool ok() const { return diagnostics.empty(); }
    };

    class NodeRegistry
    {
    public:
        [[nodiscard]] bool registerNode(NodeDescriptor descriptor);
        [[nodiscard]] const NodeDescriptor* find(std::string_view typeId) const;
        [[nodiscard]] bool contains(std::string_view typeId) const;
        [[nodiscard]] std::vector<std::string> typeIds() const;

    private:
        std::unordered_map<std::string, NodeDescriptor> m_Descriptors;
    };

    [[nodiscard]] NodeRegistry makeBuiltinNodeRegistry();
    [[nodiscard]] NodeDescriptorParseResult nodeDescriptorFromJson(const nlohmann::json& root);
    [[nodiscard]] NodeDescriptorParseResult loadNodeDescriptorFromText(std::string_view text);
    [[nodiscard]] std::vector<Diagnostic> validateGraph(const Graph& graph, const NodeRegistry& registry);

    // Per-model surface output node type ids. A surface graph must contain exactly
    // one node from this family; its typeId (not a param) selects the shading model.
    // Single source of truth shared by the validator, compiler, render system, and editor.
    [[nodiscard]] const std::vector<std::string>& surfaceOutputTypeIds();
    [[nodiscard]] bool                             isSurfaceOutputType(std::string_view typeId);

    // Maps a surface output node typeId to its ShadingModel. Returns std::nullopt for
    // non-output nodes. vultra.output.custom returns std::nullopt (its model is resolved
    // by name against the ShadingModelRegistry at compile/render time).
    [[nodiscard]] std::optional<ShadingModel> shadingModelForOutputType(std::string_view typeId);

    // Inverse of shadingModelForOutputType: the builtin output node typeId for a model.
    // Used by legacy-graph migration (the old vultra.output.surface + shadingModel param).
    [[nodiscard]] std::string_view outputTypeForShadingModel(ShadingModel model);
} // namespace vultra::material_graph
