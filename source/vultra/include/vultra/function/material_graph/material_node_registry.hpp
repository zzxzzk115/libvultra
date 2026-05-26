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
    [[nodiscard]] std::vector<Diagnostic> validateGraph(const Graph& graph, const NodeRegistry& registry);
} // namespace vultra::material_graph
