#pragma once

#include "vultra/core/rhi/render_pass.hpp"
#include "vultra/core/rhi/structs/pixel_format.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"
#include "vultra/function/rendering/srp/builtin/passes/view_synthesis_settings.hpp"

namespace vultra
{
    struct PullPushMipData
    {
        FrameGraphResource pyramid;
        FrameGraphResource output;
    };

    // One pull (coarse-to-fine) pyramid level: detects invalid/hole pixels at LOD and pulls
    // valid color down from the coarser level, depth-aware via the alpha-validity convention.
    class PullPyramidPass final : public rhi::RenderPass<PullPyramidPass>
    {
        friend class BasePass;

    public:
        PullPyramidPass();

        [[nodiscard]] PullPushMipData addPass(FrameGraphBuildContext& ctx,
                                              FrameGraphResource      pyramid,
                                              uint32_t                lod,
                                              rhi::Extent2D           dstExtent,
                                              bool                    useDepthAware,
                                              float                   depthThreshold);

    private:
        [[nodiscard]] rhi::GraphicsPipeline
        createPipeline(rhi::PixelFormat colorFormat, uint32_t viewMask, bool useDepthAware) const;
    };

    // One push (fine-to-coarse) pyramid level: propagates valid color upward to build the
    // coarse fill used by the pull stage.
    class PushPyramidPass final : public rhi::RenderPass<PushPyramidPass>
    {
        friend class BasePass;

    public:
        PushPyramidPass();

        [[nodiscard]] PullPushMipData addPass(FrameGraphBuildContext& ctx,
                                              FrameGraphResource      pyramid,
                                              uint32_t                lod,
                                              rhi::Extent2D           dstExtent,
                                              bool                    useDepthAware,
                                              float                   depthThreshold);

    private:
        [[nodiscard]] rhi::GraphicsPipeline
        createPipeline(rhi::PixelFormat colorFormat, uint32_t viewMask, bool useDepthAware) const;
    };

    // Depth-aware pull-push inpaint: repairs disocclusion holes left by a warp by building a
    // mip pyramid (push) then resolving valid color into the holes (pull). Reads holes via the
    // shared alpha-validity convention written by GeometryWarp (or any warp backend).
    class PullPushInpaintPass final
    {
    public:
        [[nodiscard]] FrameGraphResource
        addPass(FrameGraphBuildContext& ctx, FrameGraphResource warped, const ViewSynthesisSettings& settings);

    private:
        PullPyramidPass m_PullPass;
        PushPyramidPass m_PushPass;
    };
} // namespace vultra
