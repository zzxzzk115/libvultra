#pragma once

#include <vultra/servers/rendering/builtin/visibility_raster_pipeline.hpp>
#include <vultra/servers/rendering/builtin/visibility_scene_bindings.hpp>
#include <vultra/servers/rendering/graph/render_graph.hpp>

#include <vri/ext/vri_ext_color_input.h>

#include <array>
#include <memory>
#include <vector>

namespace vultra
{
    // The target borrows the three views. Release this owner before the textures.
    class VisibilityColorInput
    {
    public:
        static std::expected<std::unique_ptr<VisibilityColorInput>, std::string> create(Device& device);
        ~VisibilityColorInput();
        VisibilityColorInput(const VisibilityColorInput&)                       = delete;
        VisibilityColorInput&            operator=(const VisibilityColorInput&) = delete;
        std::expected<void, std::string> attach(Texture& ids, Texture& depth, Texture& hdr);

        bool belongsTo(const Device& device) const
        {
            return &device == &m_Device;
        }

        bool matches(const Texture& ids, const Texture& depth, const Texture& hdr) const
        {
            return m_Images == std::array {&ids, &depth, &hdr};
        }

        VriColorInputPass* pass() const
        {
            return m_Pass;
        }

        VriColorInputTarget* target() const
        {
            return m_Target;
        }

        const VriColorInputInterface& commands() const
        {
            return m_Interface;
        }

    private:
        explicit VisibilityColorInput(Device& device) :
            m_Device(device)
        {
        }

        Device&                       m_Device;
        VriColorInputInterface        m_Interface {};
        VriColorInputPass*            m_Pass   = nullptr;
        VriColorInputTarget*          m_Target = nullptr;
        std::array<const Texture*, 3> m_Images {};
    };

    struct VisibilityGraphInputs
    {
        RenderGraph::Resource ids {}, depth {}, hdr {}, clusters {}, frame {};
        // Unique imported material textures used by the scene bindings.
        std::vector<RenderGraph::Resource> materialTextures;
        // Texture descriptors from lighting slots 1 through 9 and 11, in that order.
        std::array<RenderGraph::Resource, 10> lightingTextures {};
        // Imported, initialized buffers in VisibilitySceneBindings::trackedBuffers() order.
        std::array<RenderGraph::Resource, 5> sceneBuffers {};
        VriDescriptorSet*                    frameSet = nullptr;
        std::vector<VriDescriptorSet*>       materialSets;
        std::vector<VisibilityRasterDraw>    draws;
        // Optional per-frame list. The caller owns it and updates it only after the prior submission completes.
        const std::vector<VisibilityRasterDraw>* dynamicDraws = nullptr;
        std::array<const VriDescriptor*, 12>     lighting {};
        // Optional imported shadow maps. When supplied, all four must match lighting slots 1..4.
        std::array<RenderGraph::Resource, 4> shadows {};
        const VriDescriptor*                 demand = nullptr;
        const VriDescriptor*                 repair = nullptr;
    };

    // Adds indexed raster and fragment resolve passes. The supplied pipelines, bindings and descriptors
    // must outlive the graph and all submissions. Imported buffers and textures must match the scene bindings.
    // The caller must bind the imported frame buffer in both frameSet and lighting slot 0.
    // Each view recorded before the previous submission completes needs its own scene bindings and frame set:
    // recording the resolve updates those descriptors immediately, not when the GPU executes the commands.
    std::expected<void, std::string> addVisibilityGraphPasses(RenderGraph&                     graph,
                                                              const VisibilityRasterPipeline&  raster,
                                                              const VisibilityResolvePipeline& resolve,
                                                              VisibilitySceneBindings&         scene,
                                                              std::string                      name,
                                                              VisibilityGraphInputs            inputs);

    std::expected<void, std::string> addVisibilityColorInputGraphPass(RenderGraph&                     graph,
                                                                      const VisibilityRasterPipeline&  raster,
                                                                      const VisibilityResolvePipeline& resolve,
                                                                      VisibilitySceneBindings&         scene,
                                                                      std::string                      name,
                                                                      VisibilityGraphInputs            inputs);

    // Add only indexed visibility and depth rasterization, for a separately declared shading pass.
    std::expected<void, std::string> addVisibilityRasterGraphPass(RenderGraph&                     graph,
                                                                  const VisibilityRasterPipeline&  raster,
                                                                  const VisibilityResolvePipeline& resolve,
                                                                  VisibilitySceneBindings&         scene,
                                                                  std::string                      name,
                                                                  VisibilityGraphInputs            inputs);

    // Add the ordinary fragment resolve separately from rasterization, for an explicit queue handoff.
    std::expected<void, std::string> addVisibilityShadingGraphPass(RenderGraph&                     graph,
                                                                   const VisibilityRasterPipeline&  raster,
                                                                   const VisibilityResolvePipeline& resolve,
                                                                   VisibilitySceneBindings&         scene,
                                                                   std::string                      name,
                                                                   VisibilityGraphInputs            inputs);

    // Resolve shading only where the R8_UINT hole mask is nonzero; other pixels receive sky.
    // The scene bindings and lighting descriptors must remain valid until GPU completion.
    std::expected<void, std::string> addVisibilityHoleShadingPass(RenderGraph&                     graph,
                                                                  const VisibilityResolvePipeline& pipeline,
                                                                  VisibilitySceneBindings&         scene,
                                                                  std::string                      name,
                                                                  VisibilityGraphInputs            inputs,
                                                                  RenderGraph::Resource            holes);

    // Demand is an R32_UINT sampled texture of ceil(width/2) by ceil(height/2).
    std::expected<void, std::string> addVisibilityDemandShadingPass(RenderGraph&                     graph,
                                                                    const VisibilityResolvePipeline& pipeline,
                                                                    VisibilitySceneBindings&         scene,
                                                                    std::string                      name,
                                                                    VisibilityGraphInputs            inputs,
                                                                    RenderGraph::Resource            demand);

    // Add both eyes to one graph. The scene geometry and immutable material descriptors may be shared,
    // but each eye must have distinct output textures, frame descriptors, and mutable scene bindings.
    // Discard the graph if adding the passes fails.
    std::expected<void, std::string> addStereoVisibilityGraphPasses(RenderGraph&                            graph,
                                                                    const VisibilityRasterPipeline&         raster,
                                                                    const VisibilityResolvePipeline&        resolve,
                                                                    std::array<VisibilitySceneBindings*, 2> scenes,
                                                                    std::string                             name,
                                                                    std::array<VisibilityGraphInputs, 2>    inputs);
} // namespace vultra
