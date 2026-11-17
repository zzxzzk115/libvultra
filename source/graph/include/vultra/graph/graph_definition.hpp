#pragma once

#include <vultra/core/base/error.hpp>
#include <vultra/graph/pass_catalog.hpp>

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

        std::string                                                serialize() const;
        [[nodiscard]] static std::expected<GraphDefinition, Error> parse(std::string_view text);
        [[nodiscard]] std::expected<void, Error>                   save(const std::filesystem::path& file) const;
        [[nodiscard]] static std::expected<GraphDefinition, Error> load(const std::filesystem::path& file);

        // Build into a fresh candidate graph. Discard the candidate if validation/build fails. With a `view` name,
        // the pass instances are named "<view>:<id>", and so are the resources and history they create; one graph
        // can hold the definition once per view, each with its own history. Imports and outputs keep their names.
        [[nodiscard]] std::expected<GraphBuild, Error> build(RenderGraph&                  graph,
                                                             const PassCatalog&            catalog,
                                                             std::span<const GraphBinding> imports,
                                                             std::string_view              view = {}) const;
    };
} // namespace vultra
