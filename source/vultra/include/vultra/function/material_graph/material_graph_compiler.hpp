#pragma once

#include "vultra/function/material_graph/material_graph.hpp"
#include "vultra/function/material_graph/material_node_registry.hpp"

#include <expected>
#include <filesystem>
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

        // Resolves a vultra.output.custom node's `shadingModelName` param to its GBuffer
        // model code (>= material::kFirstCustomShadingModelCode). Populated from the
        // ShadingModelRegistry by the caller; when a name is missing the compiler falls
        // back to the PBR Metallic-Roughness code so the graph still renders.
        std::unordered_map<std::string, uint32_t> customShadingModelCodes;
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

    // Wraps the SurfaceFunctionBackend output in a complete mesh-material fragment
    // (.vshader) that fills VultraMaterialEval from the graph's surface and writes
    // the GBuffer via VULTRA_MATERIAL_MAIN. This is the bridge that lets a material
    // graph render through the existing eShaderMaterial path (its per-pixel GLSL),
    // instead of the parametric (constant) runtime reduction. v1 targets graphs
    // without texture-sample nodes (constant/procedural/custom-BXDF-helper graphs).
    class MeshMaterialBackend final : public ICompileBackend
    {
    public:
        // writeEntityId emits a second, entity-id-writing variant under a distinct
        // shader id (`...material_eid.frag`) by baking `#define WRITE_ENTITY_ID 1`.
        // The project shader cook does not expand per-shader `permute` keywords, so
        // the entity-id permutation is shipped as its own cooked shader instead.
        explicit MeshMaterialBackend(bool writeEntityId = false) : m_WriteEntityId(writeEntityId) {}
        [[nodiscard]] std::string_view name() const override { return "mesh_material_fragment"; }
        [[nodiscard]] std::expected<CompileOutput, std::vector<Diagnostic>>
        compile(const CompileInput& input, const NodeRegistry& registry) const override;

    private:
        bool m_WriteEntityId {false};
    };

    [[nodiscard]] uint32_t stableGraphId(std::string_view text);
    [[nodiscard]] std::string sanitizeShaderId(std::string_view value);

    // Headless: compile every `<assetRoot>/**/*.vmatgraph.json` into its generated
    // preview + mesh-material `.vshader` under
    // `<projectRoot>/.vultra/generated/shaders/material_graph/`, registering project
    // `.vmatnode.json` custom nodes first. This is the same codegen the editor runs
    // on save, so material graphs render (and their compile errors surface) without
    // first being opened in the graph editor. Returns the number compiled; failures
    // are logged. Run before the project shader library is (re)imported so the
    // generated shaders get cooked.
    int compileProjectMaterialGraphs(const std::filesystem::path& projectRoot, const std::filesystem::path& assetRoot);
} // namespace vultra::material_graph
