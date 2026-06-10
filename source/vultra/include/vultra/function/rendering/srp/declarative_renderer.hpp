#pragma once

#include "vultra/core/base/base.hpp"
#include "vultra/core/rhi/graphics_pipeline.hpp"
#include "vultra/function/material/shading_model_registry.hpp"
#include "vultra/function/rendering/srp/renderer.hpp"

#include <sol/sol.hpp>
#include <vrendergraph/vrendergraph.hpp>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace vultra
{
    class IShaderService;

    void registerBuiltinRenderGraphPasses(vrendergraph::RenderGraphRegistry& registry);

    class DeclarativeRenderer final : public Renderer
    {
    public:
        explicit DeclarativeRenderer(std::string pipelineUri, std::string rendererKey = {});
        ~DeclarativeRenderer() override;

        std::string_view name() const override { return m_RendererKey; }

        // Custom shading models registered by the pipeline asset's Lua `ShadingModel{}`
        // descriptors. NOTE: registration + code assignment + extra-params metadata are
        // in place, but the deferred lighting path does not yet dispatch custom BXDFs
        // (it is a monolithic Cook-Torrance shader); custom-model *shading* lands with
        // the forward/clustered path. Until then a custom-model material renders with
        // the default PBR response.
        [[nodiscard]] const material::ShadingModelRegistry& shadingModelRegistry() const
        {
            return m_ShadingModelRegistry;
        }

        void init() override;
        void             buildFrameGraph(FrameGraphBuildContext& ctx) override;
        [[nodiscard]] bool prefersExplicitPerEyeStereo() const override;
        bool             updateRenderGraph(std::string_view uri);
        void             invalidateShaderPipelines();

    private:
        struct ShaderRef
        {
            std::string library {"project"};
            std::string vertexLibrary;
            std::string fragmentLibrary;
            std::string vertex;
            std::string fragment;
            std::string compute;
            std::string raygen;
            std::string miss;
            std::string closestHit;
            std::string anyHit;
        };

        struct FullscreenPass
        {
            std::string name;
            ShaderRef   shader;
            std::string input {"final_composition_source"};
            std::string output {"final_composition_source"};
        };

        struct Feature
        {
            std::string                 name;
            std::string                 builtin;
            std::string                 renderGraph;
            std::vector<FullscreenPass> fullscreenPasses;
        };

        // A fully script-authored render graph pass: Lua supplies `setup` and
        // `execute` closures that drive the FrameGraph builder and the command
        // recorder directly (see LuaPassBuildContext / LuaPassExecContext). This is
        // the standard for project render passes.
        struct ScriptedPassDef
        {
            std::string                          type;
            std::vector<std::string>             inputs;
            std::vector<std::string>             outputs;
            std::vector<vrendergraph::ParamDesc> params;
            sol::protected_function              setup;
            sol::protected_function              execute;
            // Optional static shader hint (from a `shader` table) used only to
            // auto-expose the shader's reflected params on the graph node. The
            // actual shader is still selected in `setup`.
            std::string reflectLibrary;
            std::string reflectFragment;
            std::string reflectCompute;
            // Source .lua logical path (res://...), used to key shader-resolution
            // diagnostics back to the authoring file in the code editor.
            std::string sourcePath;
        };

        struct PipelineAsset
        {
            std::string                                   rendererKey {"custom"};
            std::unordered_map<std::string, std::string> shaderLibraries;
            std::vector<std::string>                     renderGraphs;
            std::vector<Feature>                         features;
            std::vector<ScriptedPassDef>                  scriptedPasses;
            std::vector<material::ShadingModelDesc>       shadingModels;
        };

        class FullscreenPassRuntime;
        class RenderGraphRuntime;
        struct RuntimeFeature;

        // Lazily-created, renderer-lifetime sol::state that hosts scripted-pass
        // closures. Kept persistent (unlike the throwaway makeAssetLuaState) so the
        // captured `setup`/`execute` sol::functions stay valid across frames.
        sol::state& renderScriptState();

        bool loadPipelineAsset();
        bool loadFeatureAsset(std::string_view uri, Feature& outFeature);
        bool parsePipelineTable(sol::table table, PipelineAsset& outAsset);
        bool parseShadingModelTable(sol::table table, material::ShadingModelDesc& outModel);
        bool parseFeatureTable(sol::table table, Feature& outFeature);
        bool parseScriptedPassTable(sol::table table, ScriptedPassDef& outPass);
        void loadScriptedPasses();
        bool loadShaderLibraries();
        bool buildRuntimeFeatures();

    private:
        std::string m_PipelineUri;
        std::string m_RendererKeyOverride;
        std::string m_RendererKey {"custom"};

        // Declared before m_Asset so it is destroyed AFTER it: m_Asset.scriptedPasses
        // holds sol::protected_function handles that must outlive their owning state.
        std::unique_ptr<sol::state> m_RenderScriptState;

        PipelineAsset m_Asset;
        material::ShadingModelRegistry m_ShadingModelRegistry;
        std::vector<std::unique_ptr<RuntimeFeature>> m_RuntimeFeatures;
        FrameGraphBuildContext* m_CurrentBuildContext {nullptr};
        bool m_CurrentFrameApplyToneMapping {true};
    };
} // namespace vultra
