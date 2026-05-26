#pragma once

#include "vultra/function/material_graph/material_graph.hpp"
#include "vultra/function/material_graph/material_node_registry.hpp"

#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace vultra::material_graph
{
    struct CompileInput
    {
        Graph graph;
        std::string shaderId;
        uint32_t graphId {0};
    };

    struct CompileOutput
    {
        std::string shaderId;
        std::string vshaderSource;
        std::vector<Diagnostic> diagnostics;
    };

    class ICompileBackend
    {
    public:
        virtual ~ICompileBackend() = default;
        [[nodiscard]] virtual std::string_view name() const = 0;
        [[nodiscard]] virtual std::expected<CompileOutput, std::vector<Diagnostic>>
        compile(const CompileInput& input, const NodeRegistry& registry) const = 0;
    };

    class MaterialGraphCompiler
    {
    public:
        explicit MaterialGraphCompiler(NodeRegistry registry = makeBuiltinNodeRegistry());

        [[nodiscard]] const NodeRegistry& registry() const { return m_Registry; }
        [[nodiscard]] NodeRegistry&       registry() { return m_Registry; }

        [[nodiscard]] std::expected<CompileOutput, std::vector<Diagnostic>>
        compile(const CompileInput& input, const ICompileBackend& backend) const;

    private:
        NodeRegistry m_Registry;
    };

    class SurfaceFunctionBackend final : public ICompileBackend
    {
    public:
        [[nodiscard]] std::string_view name() const override { return "surface_function"; }
        [[nodiscard]] std::expected<CompileOutput, std::vector<Diagnostic>>
        compile(const CompileInput& input, const NodeRegistry& registry) const override;
    };

    [[nodiscard]] uint32_t stableGraphId(std::string_view text);
    [[nodiscard]] std::string sanitizeShaderId(std::string_view value);
} // namespace vultra::material_graph
