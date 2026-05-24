#pragma once

#include "vultra/core/base/base.hpp"
#include "vultra/core/rhi/graphics_pipeline.hpp"
#include "vultra/function/rendering/srp/renderer.hpp"

#include <sol/sol.hpp>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace vultra
{
    class IShaderService;

    class DeclarativeRenderer final : public Renderer
    {
    public:
        explicit DeclarativeRenderer(std::string pipelineUri, std::string rendererKey = {});
        ~DeclarativeRenderer() override;

        std::string_view name() const override { return m_RendererKey; }
        void             init() override;
        void             buildFrameGraph(FrameGraphBuildContext& ctx) override;

    private:
        struct ShaderRef
        {
            std::string library {"project"};
            std::string vertex;
            std::string fragment;
        };

        struct FullscreenPass
        {
            std::string name;
            ShaderRef   shader;
            std::string input {"final_composition_source"};
            std::string output {"final_composition_source"};
            float       exposure {1.0f};
            int         method {0};
            bool        pushConstants {false};
        };

        struct Feature
        {
            std::string                 name;
            std::string                 builtin;
            std::string                 renderGraph;
            std::vector<FullscreenPass> fullscreenPasses;
        };

        struct PipelineAsset
        {
            std::string                                   rendererKey {"custom"};
            std::unordered_map<std::string, std::string> shaderLibraries;
            std::vector<std::string>                     renderGraphs;
            std::vector<Feature>                         features;
        };

        class FullscreenPassRuntime;
        class RenderGraphRuntime;
        struct RuntimeFeature;

        bool loadPipelineAsset();
        bool loadFeatureAsset(std::string_view uri, Feature& outFeature);
        bool parsePipelineTable(sol::table table, PipelineAsset& outAsset);
        bool parseFeatureTable(sol::table table, Feature& outFeature);
        bool loadShaderLibraries();
        bool buildRuntimeFeatures();

    private:
        std::string m_PipelineUri;
        std::string m_RendererKeyOverride;
        std::string m_RendererKey {"custom"};

        PipelineAsset m_Asset;
        std::vector<std::unique_ptr<RuntimeFeature>> m_RuntimeFeatures;
        FrameGraphBuildContext* m_CurrentBuildContext {nullptr};
    };
} // namespace vultra
