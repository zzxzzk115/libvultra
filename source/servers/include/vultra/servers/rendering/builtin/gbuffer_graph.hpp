#pragma once

#include <vultra/servers/rendering/builtin/gbuffer_pipeline.hpp>
#include <vultra/servers/rendering/graph/render_graph.hpp>

namespace vultra
{
    struct GBufferGraphInputs
    {
        RenderGraph::Resource              frame {};
        RenderGraph::Resource              vertices {};
        RenderGraph::Resource              indices {};
        RenderGraph::Resource              transforms {};
        RenderGraph::Resource              depth {};
        std::vector<RenderGraph::Resource> materialTextures;
        VriDescriptorSet*                  frameSet = nullptr;
        std::vector<VriDescriptorSet*>     materialSets;
        std::vector<VisibilityRasterDraw>  draws;
        // Optional caller-owned list, changed only after the previous submission has completed.
        const std::vector<VisibilityRasterDraw>* dynamicDraws = nullptr;
    };

    // The caller must import every unique material texture and the frame buffer bound by the sets.
    // Keep the pipeline, descriptor sets and optional draw list alive through graph execution.
    // Discard the graph if creation fails after a pass has been added.
    std::expected<std::array<RenderGraph::Resource, 7>, std::string>
    addGBufferGraphPasses(RenderGraph&           graph,
                          const GBufferPipeline& pipeline,
                          std::string            name,
                          GBufferGraphInputs     inputs);
} // namespace vultra
