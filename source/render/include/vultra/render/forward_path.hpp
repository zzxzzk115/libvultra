#pragma once

#include <vultra/render/render_path.hpp>
#include <vultra/shader/shader_pipeline.hpp>

#include <array>
#include <expected>
#include <memory>
#include <vector>

namespace vultra
{
    // "vultra.forward": the reference render path, the stages vultra.mesh (slot geometry) and
    // vultra.forward_shading (slot shading). It has no shadows, transparency or mesh shading; those belong to
    // other paths and stages written against the same public interfaces.
    inline constexpr std::string_view kForwardPath = "vultra.forward";

    // "vultra.mesh", a frame stage for the slot geometry: imports the scene's vertex, index and primitive
    // transform buffers as scene.vertices, scene.indices and scene.instances.
    StageType meshStage();

    // "vultra.forward_shading", a view stage for the slot shading. Reads the geometry buffers and scene.lights and
    // provides scene.hdr and scene.depth: one opaque OpenPBR forward pass over a skybox, with analytic lights and
    // image-based lighting.
    StageType forwardShadingStage();

    RenderPathDesc forwardPathDesc();

    // vultra.forward from the two stages above. `shaders` is the cooked builtin shader source, or null.
    [[nodiscard]] std::expected<std::unique_ptr<RenderPath>, Error>
    createForwardPath(Device& device, const AssetSource* shaders = nullptr);

    struct ForwardSettings
    {
        glm::vec3 ambientColor {0}; // Constant fill, independent of image-based lighting.
        float     environmentIntensity = 1;
        bool      ibl                  = true;
        bool      skybox               = true;
        uint32_t  debugMode            = 0; // 0 lit, 1 base color, 2 normals, 5 emission
    };

    // The stage behind vultra.forward_shading. A path keeps it in its slot: path.stage("shading").
    class ForwardShadingStage final : public RenderStage
    {
    public:
        static constexpr std::string_view kName = "vultra.forward_shading";

        // A supplied cooked shader source must outlive the stage and its shader reloads. Graph passes and
        // pipeline builders keep the stage's address, so it lives behind a unique_ptr.
        [[nodiscard]] static std::expected<std::unique_ptr<ForwardShadingStage>, Error>
        create(Device& device, const AssetSource* shaders = nullptr);
        ~ForwardShadingStage() override;
        ForwardShadingStage(const ForwardShadingStage&)            = delete;
        ForwardShadingStage& operator=(const ForwardShadingStage&) = delete;

        std::expected<SceneResources, Error> addPasses(StageBuild& build) override;
        std::expected<void, Error>           prepare(RenderGraph& graph, const StageFrame& frame) override;
        void                                 pollShaders() override;

        // Primitives of a view that passed frustum culling in the last prepare().
        uint32_t visiblePrimitives(uint32_t view = 0) const;

        ForwardSettings settings;

    private:
        // What differs per view: its camera constants and the primitives it draws.
        struct View
        {
            std::unique_ptr<Buffer> frameBuffer;
            VriDescriptor*          frameView = nullptr;
            VriDescriptorSet*       frameSet  = nullptr;
            std::vector<uint32_t>   visible;
        };

        explicit ForwardShadingStage(Device& device);
        std::expected<void, Error> bindScene(StageBuild& build, Buffer& instances);
        void                       releaseScene();
        void drawScene(VriCommandBuffer* cmd, const View& view, Buffer& vertices, Buffer& indices);

        Device&                  m_Device;
        GpuScene*                m_Scene       = nullptr;
        Environment*             m_Environment = nullptr;
        std::unique_ptr<Texture> m_OpenPbrLuts;
        VriDescriptor*           m_EnvironmentSampler = nullptr;
        VriPipelineLayout*       m_Layout             = nullptr;
        std::vector<View>        m_Views; // Kept across graphs; a graph uses the first views.size().
        // Per-scene state, rebuilt by addPasses() for view 0.
        VriDescriptor*                 m_TransformView = nullptr;
        VriDescriptorPool*             m_Pool          = nullptr;
        std::vector<VriDescriptorSet*> m_MaterialSets;
        std::vector<VriDescriptor*>    m_MaterialSamplers;
        // Indexed by sidedness (bit 0) and mirrored front face (bit 1).
        std::array<std::unique_ptr<ShaderPipeline>, 4> m_Forward;
        std::unique_ptr<ShaderPipeline>                m_Skybox;
    };
} // namespace vultra
