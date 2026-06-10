#pragma once

#include "vultra/core/rhi/render_pass.hpp"
#include "vultra/core/rhi/structs/pixel_format.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"
#include "vultra/function/rendering/srp/builtin/passes/view_synthesis_settings.hpp"

namespace vultra
{
    // Geometry-shader warp: reprojects a source color (+ depth) into a target view by
    // rasterizing a per-pixel grid mesh displaced by the depth, detecting stretched
    // (disocclusion) primitives and encoding validity in alpha for a downstream inpaint.
    // Source/target view-projection matrices come from RenderView::multiviewCameras (stereo
    // eye synthesis); without a second camera the warp is an identity pass-through.
    class GeometryWarpPass final : public rhi::RenderPass<GeometryWarpPass>
    {
        friend class BasePass;

    public:
        GeometryWarpPass();

        [[nodiscard]] FrameGraphResource addPass(FrameGraphBuildContext&      ctx,
                                                 FrameGraphResource           source,
                                                 FrameGraphResource           depth,
                                                 const ViewSynthesisSettings& settings);

    private:
        [[nodiscard]] rhi::GraphicsPipeline createPipeline(rhi::PixelFormat colorFormat, uint32_t viewMask) const;
    };
} // namespace vultra
