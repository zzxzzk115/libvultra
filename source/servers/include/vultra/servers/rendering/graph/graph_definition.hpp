#pragma once

#include <vultra/servers/rendering/graph/pass_catalog.hpp>

#include <expected>
#include <filesystem>

namespace vultra
{
    struct GraphPassDesc
    {
        std::string    id;
        std::string    type;
        PassParameters parameters;
    };

    struct GraphEdge
    {
        std::string from;
        std::string to;
    };

    struct GraphBinding
    {
        std::string           name;
        RenderGraph::Resource resource;
    };

    struct GraphBuild
    {
        std::vector<BuiltPass>    passes;
        std::vector<GraphBinding> outputs;
    };

    // A version-1 description compiled into the existing code-driven RenderGraph.
    struct GraphDefinition
    {
        std::vector<GraphPassDesc> passes;
        std::vector<GraphEdge>     edges;
        std::vector<std::string>   outputs;

        // Checked version-1 persistence, bounded to 64 MiB. Invalid UTF-8 and nonfinite parameters reject.
        // Semantic connection/capability validation belongs to buildExpected(), so editable partial graphs persist.
        std::expected<std::string, std::string>            serializeExpected() const;
        static std::expected<GraphDefinition, std::string> parseExpected(std::string_view text);
        // Atomic publication preserves the previous file on validation or write failure.
        std::expected<void, std::string>                   saveExpected(const std::filesystem::path& file) const;
        static std::expected<GraphDefinition, std::string> loadExpected(const std::filesystem::path& file);

        // Append only; call between completed GPU uses. Failure rolls back all appended passes/resources/exports.
        // Keep the successful GraphBuild alive until its graph callbacks and final GPU uses are discarded.
        GraphBuild build(RenderGraph& graph, const PassCatalog& catalog, std::span<const GraphBinding> imports) const;
        std::expected<GraphBuild, std::string>
        buildExpected(RenderGraph& graph, const PassCatalog& catalog, std::span<const GraphBinding> imports) const;
    };
} // namespace vultra
