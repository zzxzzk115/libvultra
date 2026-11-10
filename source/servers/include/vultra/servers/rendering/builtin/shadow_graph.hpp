#pragma once

#include <vultra/servers/rendering/builtin/shadow_raster_pipeline.hpp>
#include <vultra/servers/rendering/graph/render_graph.hpp>

namespace vultra
{
    struct ShadowGraphInputs
    {
        std::array<RenderGraph::Resource, 4> depths {};
        RenderGraph::Resource                vertices {}, indices {}, transforms {}, frame {};
        // Every unique base-color texture sampled by the shadow shader must be imported once here.
        std::vector<RenderGraph::Resource>               materialTextures;
        VriDescriptorSet*                                frameSet = nullptr;
        std::vector<VriDescriptorSet*>                   materialSets;
        std::array<std::vector<VisibilityRasterDraw>, 4> draws;
        // Optional caller-owned lists updated only after the previous submission completes.
        std::array<const std::vector<VisibilityRasterDraw>*, 4> dynamicDraws {};
        // When supplied, false preserves the previous depth contents. Set true before first use and after invalidation.
        // Change only after the preceding submission completes; the caller owns failure and invalidation handling.
        std::array<const bool*, 4> redraw {};
    };

    // Adds four clear-and-draw depth passes. The pipeline, descriptors and imported resources
    // must outlive the graph and its submissions. Discard the graph if adding passes fails.
    std::expected<void, std::string> addShadowRasterGraphPasses(RenderGraph&                graph,
                                                                const ShadowRasterPipeline& pipeline,
                                                                std::string                 name,
                                                                ShadowGraphInputs           inputs);
} // namespace vultra
