#pragma once

#include <vultra/assets/project_manifest.hpp>
#include <vultra/drivers/profiling/profiler.hpp>
#include <vultra/scene/camera/orbit_camera.hpp>
#include <vultra/scene/scene_import.hpp>
#include <vultra/scene/scene_render_state.hpp>
#include <vultra/scene/scene_shader_materials.hpp>
#include <vultra/scene/scene_tree.hpp>
#include <vultra/servers/rendering/builtin/builtin_renderer.hpp>
#include <vultra/servers/rendering/builtin/reference_path_tracer.hpp>
#include <vultra/servers/rendering/graph/graph_definition.hpp>
#include <vultra/servers/rendering/rendering_server.hpp>

#include <functional>
#include <map>
#include <optional>

namespace vultra
{
    // Tool state, separate from the project and portable .vgraph pass definition. All formats remain version 1.
    struct ResearchDocument
    {
        std::filesystem::path project;
        GraphDefinition       definition;
        // Empty selects the project's entry scene; saved workspaces capture the current SceneTree.
        std::string                      sceneSnapshot;
        RenderSettings                   settings;
        Extent                           size {640, 360};
        uint32_t                         seed = 0;
        std::optional<OrbitCamera>       camera;
        std::map<std::string, glm::vec2> nodePositions;

        void                    save(const std::filesystem::path& file) const;
        static ResearchDocument load(const std::filesystem::path& file);
    };

    struct ResearchGraph
    {
        explicit ResearchGraph(Device& device) :
            graph(device)
        {
        }

        BuiltinRenderer::Outputs     rendererOutputs;
        std::unique_ptr<PassCatalog> rasterCatalog;
        // Reverse destruction discards callbacks before destroying pass instances.
        std::unique_ptr<ReferencePathTracer> reference;
        ReferencePathTracer::Outputs         referenceOutputs {};
        GraphBuild                           instances;
        RenderGraph                          graph;
        std::vector<GraphBinding>            previews;
        RenderGraph::Snapshot                snapshot;
    };

    // Used by the workbench and its windowless GPU regression. The caller owns submission/completion.
    class ResearchWorkspace
    {
    public:
        using BeforeReplace = std::function<void(ResearchGraph&)>;

        ResearchWorkspace(Device& device, RenderingServer& server, const PassCatalog& catalog);
        // Previous GPU/GUI use must be complete. A failed candidate leaves all active state intact.
        void replace(const ResearchDocument& document, const BeforeReplace& releasePreviews = {});
        // Same completion boundary as replace(), but retains the compiled graph and preview descriptors.
        void setPassParameters(std::string_view id, const PassParameters& parameters);
        // Build replacement lighting before publishing scene selection/asset changes. Zero selects the project preset.
        void setEnvironment(ObjectId node, AssetId radiance);
        void prepareFrame(); // After GPU completion, before command recording.
        void record(VriCommandBuffer* cmd, Profiler* profiler = nullptr);
        void completeFrame(); // After submission and GPU completion, before editing or exporting.
        void save(const std::filesystem::path& file) const;
        void exportImages(const std::filesystem::path& directory, std::span<const PassTiming> timings = {});

        const ResearchDocument& document() const;
        ResearchGraph&          graph();
        OrbitCamera&            camera();
        SceneTree&              scene();
        const ProjectManifest&  project() const;
        GpuSceneRid             sceneRid() const;
        // Imported slots belong to this mesh instance; overrides can only target these numeric material ranges.
        uint32_t           materialSlotCount(ObjectId mesh) const;
        const ShaderAsset& shaderAsset(ObjectId material);
        std::string        shaderDiagnostics() const;

    private:
        struct Scene
        {
            std::filesystem::path                 projectPath;
            ProjectManifest                       project;
            std::unique_ptr<SceneTree>            tree;
            SceneRenderState                      renderState;
            std::vector<SceneMeshInstance>        instances;
            SceneGpuSync                          gpuSync;
            GpuSceneHandle                        gpu;
            std::unique_ptr<Environment>          environment;
            std::unique_ptr<SceneShaderMaterials> shaderMaterials;
            std::unique_ptr<BuiltinRenderer>      renderer;
        };

        Device&                        m_Device;
        RenderingServer&               m_Server;
        const PassCatalog&             m_Catalog;
        ResearchDocument               m_Document;
        OrbitCamera                    m_Camera;
        std::unique_ptr<Scene>         m_Scene;
        std::unique_ptr<ResearchGraph> m_Graph;
    };
} // namespace vultra
