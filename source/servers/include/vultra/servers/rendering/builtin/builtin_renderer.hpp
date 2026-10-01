#pragma once

#include <vultra/core/base/api_annotations.hpp>
#include <vultra/drivers/rhi/shader_pipeline.hpp>
#include <vultra/servers/rendering/builtin/environment.hpp>
#include <vultra/servers/rendering/builtin/shadow_cascades.hpp>
#include <vultra/servers/rendering/graph/render_graph.hpp>
#include <vultra/servers/rendering/scene.hpp>

#include <vri/ext/vri_ext_meshshader.h>

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace vultra
{
    enum class RenderPath
    {
        eNaiveDeferred,
        eNaiveForward
    };

    enum class ShadowFilter
    {
        eDisabled,
        eHard,
        ePcf,
        ePcss
    };

    struct VULTRA_REFLECT RenderSettings
    {
        VULTRA_PROPERTY("label=Path;options=NaiveDeferred|NaiveForward")
        RenderPath path = RenderPath::eNaiveDeferred;
        glm::vec3  directionToLight {-0.5f, 0.8f, 0.4f};
        glm::vec3  lightColor {1, 0.95f, 0.85f};
        VULTRA_PROPERTY("label=Sun intensity;min=0;max=10")
        float lightIntensity       = 3;
        float environmentIntensity = 1;
        VULTRA_PROPERTY("label=Exposure (EV);min=-4;max=4")
        float exposure = 0;
        VULTRA_PROPERTY("label=IBL")
        bool ibl = true;
        VULTRA_PROPERTY("label=Skybox")
        bool         skybox            = true;
        bool         meshShading       = false; // Requires GpuScene meshlets and VriFeature_MeshShader.
        bool         meshletCulling    = true;
        bool         meshletColors     = false;
        ShadowFilter shadowFilter      = ShadowFilter::ePcf;
        uint32_t     shadowResolution  = 1024; // Change before assembling the graph.
        float        splitLambda       = 0.7f;
        float        shadowBias        = 0.0005f; // normalized cascade depth
        float        normalBias        = 0.5f;    // shadow texels in world space
        float        sunAngularRadius  = 0.02f;   // radians; PCSS penumbra control
        float        cascadeBlend      = 0.1f;
        float        roughnessOverride = -1;
        float        metalnessOverride = -1;
        uint32_t     debugMode         = 0; // 0 lit, 1 base color, 2 normals, 3 cascades, 4 visibility, 5 emission
    };

    class BuiltinRenderer
    {
    public:
        using ShadowMaps = std::array<RenderGraph::Resource, 4>;

        struct Outputs
        {
            RenderPath                           path = RenderPath::eNaiveDeferred;
            ShadowMaps                           shadows;
            std::array<RenderGraph::Resource, 7> gbuffer;
            RenderGraph::Resource                hdr;
            RenderGraph::Resource                depth;
            RenderGraph::Resource                color;
        };

        // UNORM output is display encoded; RGBA16_SFLOAT is tone mapped but linear (for XR).
        BuiltinRenderer(Device&      device,
                        GpuScene&    scene,
                        Environment& environment,
                        VriFormat    outputFormat = VriFormat_RGBA8_UNORM);
        ~BuiltinRenderer();
        BuiltinRenderer(const BuiltinRenderer&)            = delete;
        BuiltinRenderer& operator=(const BuiltinRenderer&) = delete;

        // C++ composition entry points. Resources returned here can feed custom research passes.
        ShadowMaps addShadowPasses(RenderGraph& graph);
        void       addSkyboxPass(RenderGraph& graph, RenderGraph::Resource hdr);
        void
        addForwardPass(RenderGraph& graph, RenderGraph::Resource hdr, RenderGraph::Resource depth, ShadowMaps shadows);

        RenderGraph::Resource addToneMappingPass(RenderGraph& graph, RenderGraph::Resource hdr, Extent size);
        Outputs               addPasses(RenderGraph& graph, Extent size);

        // After graph.compile(), before recording. Previous frame must have completed.
        void           prepare(const RenderCamera& camera, RenderGraph& graph, const Outputs& outputs);
        void           pollShaders();
        std::string    diagnostics() const;
        RenderSettings settings;

    private:
        enum class GeometryPass
        {
            eShadow,
            eForward,
            eGBufferBase,
            eGBufferMaterial
        };

        void                           release();
        void                           drawScene(VriCommandBuffer* cmd, GeometryPass pass, uint32_t cascade = 0);
        void                           addDeferredPasses(RenderGraph& graph, Outputs& outputs, Extent size);
        void                           drawFullscreen(VriCommandBuffer* cmd, VriPipeline* pipeline);
        Device&                        m_Device;
        GpuScene&                      m_Scene;
        Environment&                   m_Environment;
        VriFormat                      m_OutputFormat;
        std::unique_ptr<Texture>       m_OpenPbrLuts;
        std::unique_ptr<Buffer>        m_FrameBuffer;
        VriDescriptor*                 m_FrameView          = nullptr;
        VriDescriptor*                 m_EnvironmentSampler = nullptr;
        VriDescriptorPool*             m_Pool               = nullptr;
        VriPipelineLayout*             m_Layout             = nullptr;
        VriDescriptorSet*              m_FrameSet           = nullptr;
        VriDescriptorSet*              m_MeshletSet         = nullptr;
        VriMeshShaderInterface         m_MeshApi {};
        std::vector<VriDescriptorSet*> m_MaterialSets;
        std::vector<VriDescriptor*>    m_MaterialSamplers;
        // Geometry pipelines are indexed by doubleSided.
        std::array<std::unique_ptr<ShaderPipeline>, 2> m_Shadow;
        std::array<std::unique_ptr<ShaderPipeline>, 2> m_Forward;
        std::array<std::unique_ptr<ShaderPipeline>, 2> m_GBufferBase;
        std::array<std::unique_ptr<ShaderPipeline>, 2> m_GBufferMaterial;
        std::array<std::unique_ptr<ShaderPipeline>, 2> m_MeshForward;
        std::unique_ptr<ShaderPipeline>                m_Skybox;
        std::unique_ptr<ShaderPipeline>                m_ToneMapping;
        std::unique_ptr<ShaderPipeline>                m_DeferredLighting;
    };
} // namespace vultra
