#pragma once

#include <vultra/servers/rendering/builtin/frame_data.hpp>
#include <vultra/servers/rendering/builtin/visibility_resolve_pipeline.hpp>
#include <vultra/servers/rendering/scene.hpp>

namespace vultra
{
    struct VisibilitySceneInputs
    {
        Buffer*                                                      vertices   = nullptr;
        Buffer*                                                      indices    = nullptr;
        Buffer*                                                      transforms = nullptr;
        std::span<const ScenePrimitive>                              primitives;
        std::span<const SurfaceMaterial>                             materials;
        std::span<const std::array<Texture*, kMaterialTextureCount>> materialTextures;
        size_t samplerCount = 0; // Imported samplers; the separate default sampler is supplied first.
    };

    // Owns PVW's indexed visibility scene descriptors. The scene, its textures/samplers and pipeline
    // must outlive this object; update materials and views only after the previous frame has completed.
    // Recreate when primitive topology, material textures, or sampler selections change.
    class VisibilitySceneBindings
    {
    public:
        static std::expected<std::unique_ptr<VisibilitySceneBindings>, std::string>
        create(Device&                          device,
               GpuScene&                        scene,
               const VisibilityResolvePipeline& pipeline,
               std::span<VriDescriptor* const>  samplers,
               Buffer*                          clusters = nullptr);
        static std::expected<std::unique_ptr<VisibilitySceneBindings>, std::string>
        create(Device&                          device,
               VisibilitySceneInputs            scene,
               const VisibilityResolvePipeline& pipeline,
               std::span<VriDescriptor* const>  samplers,
               Buffer*                          clusters = nullptr);
        ~VisibilitySceneBindings();
        VisibilitySceneBindings(const VisibilitySceneBindings&)            = delete;
        VisibilitySceneBindings& operator=(const VisibilitySceneBindings&) = delete;

        std::expected<void, std::string> updateMaterials();

        // Opt into frame specialization using the values already uploaded to lighting slot 0.
        // Call after each successful frame upload, after the prior submission completes and before recording.
        // Without this opt-in, the complete frame-feature program remains selected.
        void setUploadedFrame(const BuiltinFrameData& frame)
        {
            m_SceneLightsOnly = usesOnlySceneLights(frame);
        }

        bool usesUncoatedShader() const
        {
            return resolvePipeline() != m_Pipeline.handle();
        }

        bool usesSceneLightsShader() const
        {
            return m_Uncoated && m_SceneLightsOnly && m_Pipeline.sceneLightsHandle();
        }

        VriPipeline* resolvePipeline() const
        {
            if (usesSceneLightsShader())
            {
                return m_Pipeline.sceneLightsHandle();
            }
            return m_Uncoated && m_Pipeline.uncoatedHandle() ? m_Pipeline.uncoatedHandle() : m_Pipeline.handle();
        }

        // Geometry views are visibility IDs, storage HDR, shading demand and repair mask, respectively.
        std::expected<void, std::string> updateFrame(const std::array<const VriDescriptor*, 12>& lighting,
                                                     const std::array<const VriDescriptor*, 4>&  geometry);
        void                             transitionGeometry(VriCommandBuffer*     commands,
                                                            VriPipelineStageFlags stages = VriPipelineStage_FragmentShader);
        // A batched pipeline requires a valid material index; a direct pipeline ignores it.
        std::expected<void, std::string> bind(VriCommandBuffer* commands, size_t material = 0) const;

        bool belongsTo(const Device& device, const VisibilityResolvePipeline& pipeline) const
        {
            return &device == &m_Device && &pipeline == &m_Pipeline;
        }

        Buffer* clusterBuffer() const
        {
            return m_Clusters;
        }

        std::span<const std::array<Texture*, kMaterialTextureCount>> materialTextures() const
        {
            return m_Scene.materialTextures;
        }

        std::array<Buffer*, 5> trackedBuffers() const
        {
            return {m_Scene.vertices, m_Scene.indices, m_Scene.transforms, m_Primitives.get(), m_Materials.get()};
        }

    private:
        VisibilitySceneBindings(Device&                          device,
                                VisibilitySceneInputs            scene,
                                const VisibilityResolvePipeline& pipeline) :
            m_Device(device),
            m_Scene(scene),
            m_Pipeline(pipeline)
        {
        }

        Device&                                       m_Device;
        VisibilitySceneInputs                         m_Scene;
        const VisibilityResolvePipeline&              m_Pipeline;
        Buffer*                                       m_Clusters    = nullptr;
        VriDescriptor*                                m_ClusterView = nullptr;
        std::unique_ptr<Buffer>                       m_Primitives;
        std::unique_ptr<Buffer>                       m_Materials;
        std::array<VriDescriptor*, 4>                 m_Views {};
        std::vector<VriDescriptor*>                   m_VertexViews;
        VriDescriptorPool*                            m_Pool = nullptr;
        std::array<VriDescriptorSet*, 4>              m_Sets {};
        std::vector<std::array<VriDescriptorSet*, 2>> m_MaterialSets;
        bool                                          m_Uncoated        = false;
        bool                                          m_SceneLightsOnly = false;
    };

    // Fragment resolve of indexed visibility IDs. Auxiliary views are storage HDR, demand and repair mask;
    // they must be valid even though the fragment shader does not read them. Lighting resources are prepared by
    // the caller and must remain alive and shader-readable through submission completion.
    std::expected<void, std::string> recordVisibilityResolve(Device&                                     device,
                                                             const VisibilityResolvePipeline&            pipeline,
                                                             VisibilitySceneBindings&                    scene,
                                                             VriCommandBuffer*                           commands,
                                                             Texture&                                    ids,
                                                             Texture&                                    hdr,
                                                             const std::array<const VriDescriptor*, 12>& lighting,
                                                             const std::array<const VriDescriptor*, 3>&  auxiliary);

    // Resolve only marked holes; all other output pixels receive the same sky value as the original PVW pass.
    std::expected<void, std::string> recordVisibilityHoleShading(Device&                                     device,
                                                                 const VisibilityResolvePipeline&            pipeline,
                                                                 VisibilitySceneBindings&                    scene,
                                                                 VriCommandBuffer*                           commands,
                                                                 Texture&                                    ids,
                                                                 Texture&                                    hdr,
                                                                 Texture&                                    holes,
                                                                 const std::array<const VriDescriptor*, 12>& lighting,
                                                                 const VriDescriptor*                        demand);

    // Each R32_UINT demand texel controls a 2x2 pixel quad: 0 is quarter, 1 half, 2 full rate.
    std::expected<void, std::string> recordVisibilityDemandShading(Device&                                     device,
                                                                   const VisibilityResolvePipeline&            pipeline,
                                                                   VisibilitySceneBindings&                    scene,
                                                                   VriCommandBuffer*                           commands,
                                                                   Texture&                                    ids,
                                                                   Texture&                                    hdr,
                                                                   Texture&                                    demand,
                                                                   const std::array<const VriDescriptor*, 12>& lighting,
                                                                   const VriDescriptor*                        repair);
} // namespace vultra
