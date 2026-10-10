#pragma once

#include <vultra/core/base/api_annotations.hpp>
#include <vultra/drivers/rhi/shader_pipeline.hpp>
#include <vultra/servers/rendering/builtin/environment.hpp>
#include <vultra/servers/rendering/builtin/render_light.hpp>
#include <vultra/servers/rendering/builtin/shadow_cascades.hpp>
#include <vultra/servers/rendering/builtin/tone_mapping_pass.hpp>
#include <vultra/servers/rendering/graph/render_graph.hpp>
#include <vultra/servers/rendering/scene.hpp>
#include <vultra/servers/rendering/shader_material.hpp>

#include <vri/ext/vri_ext_meshshader.h>

#include <array>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace vultra
{
    enum class RenderPath
    {
        eNaiveDeferred,
        eNaiveForward,
        eReferencePathTracing
    };

    enum class ShadowFilter
    {
        eDisabled,
        eHard,
        ePcf,
        ePcss
    };

    enum class ToneOperator
    {
        eAces,
        eNone,
        eReinhard
    };

    struct VULTRA_REFLECT RenderSettings
    {
        VULTRA_PROPERTY("label=Path;options=NaiveDeferred|NaiveForward")
        RenderPath path = RenderPath::eNaiveDeferred;
        VULTRA_PROPERTY("label=directionToLight;flags=serialize|bind")
        glm::vec3 directionToLight {-0.5f, 0.8f, 0.4f};
        VULTRA_PROPERTY("label=lightColor;flags=serialize|bind")
        glm::vec3 lightColor {1, 0.95f, 0.85f};
        VULTRA_PROPERTY("label=Ambient color;flags=serialize|bind")
        glm::vec3 ambientColor {0}; // Optional constant raster fill, independent of IBL and direct-light shadows.
        VULTRA_PROPERTY("label=Sun intensity;min=0;max=10")
        float lightIntensity = 3;
        VULTRA_PROPERTY("label=environmentIntensity;flags=serialize|bind")
        float environmentIntensity = 1;
        VULTRA_PROPERTY("label=Exposure (EV);min=-4;max=4")
        float exposure = 0;
        VULTRA_PROPERTY("label=Tone operator;options=Aces|None|Reinhard")
        ToneOperator toneOperator = ToneOperator::eAces;
        VULTRA_PROPERTY("label=IBL")
        bool ibl = true;
        VULTRA_PROPERTY("label=Skybox")
        bool skybox = true;
        VULTRA_PROPERTY("label=meshShading;flags=serialize|bind")
        bool meshShading = false; // Requires GpuScene meshlets and VriFeature_MeshShader.
        VULTRA_PROPERTY("label=meshletCulling;flags=serialize|bind")
        bool meshletCulling = true;
        VULTRA_PROPERTY("label=meshletColors;flags=serialize|bind")
        bool meshletColors = false;
        VULTRA_PROPERTY("label=shadowFilter;flags=serialize|bind")
        ShadowFilter shadowFilter = ShadowFilter::ePcf;
        VULTRA_PROPERTY("label=Cache static shadows")
        bool cacheShadows = false; // Opt in for immutable imported geometry/texture contents.
        VULTRA_PROPERTY("label=shadowResolution;flags=serialize|bind")
        uint32_t shadowResolution = 1024; // Change before assembling the graph.
        VULTRA_PROPERTY("label=splitLambda;flags=serialize|bind")
        float splitLambda = 0.7f;
        VULTRA_PROPERTY("label=shadowBias;flags=serialize|bind")
        float shadowBias = 0.0005f; // normalized cascade depth
        VULTRA_PROPERTY("label=normalBias;flags=serialize|bind")
        float normalBias = 0.5f; // shadow texels in world space
        VULTRA_PROPERTY("label=sunAngularRadius;flags=serialize|bind")
        float sunAngularRadius = 0.02f; // radians; PCSS penumbra control
        VULTRA_PROPERTY("label=cascadeBlend;flags=serialize|bind")
        float cascadeBlend = 0.1f;
        VULTRA_PROPERTY("label=roughnessOverride;flags=serialize|bind")
        float roughnessOverride = -1;
        VULTRA_PROPERTY("label=metalnessOverride;flags=serialize|bind")
        float metalnessOverride = -1;
        VULTRA_PROPERTY("label=debugMode;flags=serialize|bind")
        uint32_t debugMode = 0; // 0 lit, 1 base color, 2 normals, 3 cascades, 4 visibility, 5 emission
    };

    class BuiltinRenderer
    {
    public:
        using GBuffer    = std::array<RenderGraph::Resource, 7>;
        using ShadowMaps = std::array<RenderGraph::Resource, 4>;

        struct Outputs
        {
            RenderPath                           path = RenderPath::eNaiveDeferred;
            ShadowMaps                           shadows {};
            std::array<RenderGraph::Resource, 7> gbuffer {};
            RenderGraph::Resource                hdr {};
            RenderGraph::Resource                depth {};
            RenderGraph::Resource                color {};
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

        GBuffer addGBufferPasses(RenderGraph& graph, RenderGraph::Resource depth, Extent size);
        void
        addDeferredLightingPass(RenderGraph& graph, RenderGraph::Resource hdr, GBuffer gbuffer, ShadowMaps shadows);

        RenderGraph::Resource addToneMappingPass(RenderGraph& graph, RenderGraph::Resource hdr, Extent size);
        // Build linear scene outputs so a project pass can run before tone mapping.
        Outputs addScenePasses(RenderGraph& graph, Extent size);
        Outputs addPasses(RenderGraph& graph, Extent size);

        // After graph.compile(), before recording. Previous frame must have completed.
        // Absent lighting uses RenderSettings' C++ sun. An explicit empty span disables direct lights.
        // Environment intensity multiplies the experimental RenderSettings intensity without changing GPU resources.
        void prepare(const RenderCamera&                         camera,
                     RenderGraph&                                graph,
                     const Outputs&                              outputs,
                     std::optional<std::span<const RenderLight>> lights               = std::nullopt,
                     float                                       environmentIntensity = 1);
        void pollShaders();
        // Borrowed material must outlive the renderer. Null restores the imported OpenPBR material.
        void setShaderMaterial(uint32_t slot, ShaderMaterial* material);
        // Prepare an isolated reload candidate against the current graph without replacing any live material.
        std::array<VriPipeline*, 10>
        prepareShaderMaterial(ShaderMaterial& material, RenderGraph& graph, const Outputs& outputs);
        // Returns an empty reason for a SubShader that meets the built-in raster ABI.
        static std::string shaderSubshaderCompatibility(const ShaderSubshader& subshader);
        // Prepare only the tone-mapping stage when an external/reference renderer supplies scene color.
        void        prepareToneMapping(RenderGraph& graph, RenderGraph::Resource hdr);
        std::string diagnostics() const;
        // Camera-visible primitives followed by submitted cascade caster counts (zero for cached maps).
        std::array<uint32_t, 5> primitiveCounts() const;
        // Publish recorded shadow contents only after their GPU submission completes.
        void completeFrame();
        // Required after in-place GPU writes to geometry/alpha textures or replacement of the graph's shadow maps.
        void           invalidateShadowCache();
        RenderSettings settings;

    private:
        enum class GeometryPass
        {
            eShadow,
            eForward,
            eGBufferBase,
            eGBufferMaterial
        };

        void                                 release();
        void                                 drawScene(VriCommandBuffer* cmd, GeometryPass pass, uint32_t cascade = 0);
        void                                 drawFullscreen(VriCommandBuffer* cmd, VriPipeline* pipeline);
        Device&                              m_Device;
        GpuScene&                            m_Scene;
        Environment&                         m_Environment;
        VriFormat                            m_OutputFormat;
        std::unique_ptr<Texture>             m_OpenPbrLuts;
        std::unique_ptr<Buffer>              m_FrameBuffer;
        VriDescriptor*                       m_FrameView          = nullptr;
        VriDescriptor*                       m_TransformView      = nullptr;
        VriDescriptor*                       m_EnvironmentSampler = nullptr;
        VriDescriptorPool*                   m_Pool               = nullptr;
        VriPipelineLayout*                   m_Layout             = nullptr;
        VriDescriptorSet*                    m_FrameSet           = nullptr;
        VriDescriptorSet*                    m_MeshletSet         = nullptr;
        VriMeshShaderInterface               m_MeshApi {};
        std::array<std::vector<uint32_t>, 5> m_VisiblePrimitives;

        struct ShadowKey
        {
            const RenderGraph*      graph    = nullptr;
            uint32_t                resource = 0;
            glm::mat4               matrix {1};
            uint64_t                transformRevision = 0;
            std::array<uint64_t, 4> programs {};
            bool                    draw                               = false;
            bool                    operator==(const ShadowKey&) const = default;
        };

        struct ShadowMaterial
        {
            float                alpha                                   = 0;
            float                cutoff                                  = 0;
            int32_t              sampler                                 = 0;
            bool                 doubleSided                             = false;
            const VriDescriptor* texture                                 = nullptr;
            bool                 operator==(const ShadowMaterial&) const = default;
        };

        std::array<ShadowKey, 4>                  m_PreparedShadowKeys;
        std::array<std::optional<ShadowKey>, 4>   m_ShadowCache;
        std::array<std::optional<ShadowKey>, 4>   m_RecordedShadows;
        std::array<bool, 4>                       m_ShadowDrawn {};
        std::vector<ShadowMaterial>               m_ShadowMaterials;
        bool                                      m_CacheShadows = false;
        std::vector<VriDescriptorSet*>            m_MaterialSets;
        std::vector<ShaderMaterial*>              m_ShaderMaterials;
        std::vector<std::array<VriPipeline*, 10>> m_ShaderPipelines;
        std::vector<VriDescriptor*>               m_MaterialSamplers;
        // Geometry pipelines are indexed by sidedness and reflected front face.
        std::array<std::unique_ptr<ShaderPipeline>, 4> m_Shadow;
        std::array<std::unique_ptr<ShaderPipeline>, 4> m_Forward;
        std::array<std::unique_ptr<ShaderPipeline>, 4> m_GBufferBase;
        std::array<std::unique_ptr<ShaderPipeline>, 4> m_GBufferMaterial;
        std::array<std::unique_ptr<ShaderPipeline>, 4> m_MeshForward;
        std::unique_ptr<ShaderPipeline>                m_Skybox;
        std::unique_ptr<ToneMappingPass>               m_ToneMapping;
        std::array<double, 3>                          m_ToneParameters {};
        std::unique_ptr<ShaderPipeline>                m_DeferredLighting;
    };
} // namespace vultra
