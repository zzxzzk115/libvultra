#pragma once

#include <vultra/servers/rendering/graph/pass_catalog.hpp>

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

        std::string            serialize() const;
        static GraphDefinition parse(std::string_view text);
        void                   save(const std::filesystem::path& file) const;
        static GraphDefinition load(const std::filesystem::path& file);

        // Build into a fresh candidate graph. Discard the candidate if validation/build fails.
        GraphBuild build(RenderGraph& graph, const PassCatalog& catalog, std::span<const GraphBinding> imports) const;
    };
} // namespace vultra
