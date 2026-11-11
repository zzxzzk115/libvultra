#pragma once

#include <vultra/render/render_path.hpp>
#include <vultra/shader/shader_pipeline.hpp>

#include <array>
#include <memory>
#include <vector>

namespace vultra
{
    class AssetSource;

    struct ForwardSettings
    {
        glm::vec3 ambientColor {0}; // Constant fill, independent of image-based lighting.
        float     environmentIntensity = 1;
        bool      ibl                  = true;
        bool      skybox               = true;
        uint32_t  debugMode            = 0; // 0 lit, 1 base color, 2 normals, 5 emission
    };

    // "vultra.forward": the reference render path. One opaque OpenPBR forward pass over a skybox, with
    // analytic lights and image-based lighting. It has no shadows, transparency or mesh shading; those
    // belong to other render paths. It uses only the public RenderPath, GpuScene and RenderGraph interfaces.
    class ForwardPath final : public RenderPath
    {
    public:
        static constexpr std::string_view kName = "vultra.forward";

        // A supplied cooked shader source must outlive the path and its shader reloads.
        explicit ForwardPath(Device& device, const AssetSource* shaders = nullptr);
        ~ForwardPath() override;
        ForwardPath(const ForwardPath&)            = delete;
        ForwardPath& operator=(const ForwardPath&) = delete;

        std::span<const SceneResourceDesc> provides() const override;
        SceneResources addPasses(RenderGraph& graph, GpuScene& scene, Environment& environment, Extent size) override;
        void prepare(RenderGraph& graph, const RenderCamera& camera, std::span<const RenderLight> lights) override;
        void pollShaders() override;

        // Primitives that passed frustum culling in the last prepare().
        uint32_t visiblePrimitives() const;

        ForwardSettings settings;

    private:
        void bindScene(GpuScene& scene, Environment& environment);
        void releaseScene();
        void drawScene(VriCommandBuffer* cmd);

        Device&                  m_Device;
        GpuScene*                m_Scene       = nullptr;
        Environment*             m_Environment = nullptr;
        std::unique_ptr<Texture> m_OpenPbrLuts;
        std::unique_ptr<Buffer>  m_FrameBuffer;
        VriDescriptor*           m_FrameView          = nullptr;
        VriDescriptor*           m_EnvironmentSampler = nullptr;
        VriPipelineLayout*       m_Layout             = nullptr;
        // Per-scene state, rebuilt by addPasses().
        VriDescriptor*                 m_TransformView = nullptr;
        VriDescriptorPool*             m_Pool          = nullptr;
        VriDescriptorSet*              m_FrameSet      = nullptr;
        std::vector<VriDescriptorSet*> m_MaterialSets;
        std::vector<VriDescriptor*>    m_MaterialSamplers;
        std::vector<uint32_t>          m_Visible;
        // Indexed by sidedness (bit 0) and mirrored front face (bit 1).
        std::array<std::unique_ptr<ShaderPipeline>, 4> m_Forward;
        std::unique_ptr<ShaderPipeline>                m_Skybox;
    };
} // namespace vultra
