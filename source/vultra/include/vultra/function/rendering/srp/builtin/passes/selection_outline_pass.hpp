#pragma once

#include "vultra/core/rhi/render_pass.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"
#include "vultra/function/rendering/render_structs.hpp"

#include <fg/Fwd.hpp>

namespace vultra
{
    class SelectionOutlinePass final : public rhi::RenderPass<SelectionOutlinePass>
    {
        friend class BasePass;

    public:
        SelectionOutlinePass();

        FrameGraphResource addPass(FrameGraphBuildContext&                         ctx,
                                   FrameGraphResource                              source,
                                   FrameGraphResource                              entityId,
                                   FrameGraphResource                              depth,
                                   const BuiltinRenderSettings::SelectionOutlineSettings& settings);

    private:
        rhi::GraphicsPipeline createPipeline(rhi::PixelFormat colorFormat) const;
    };
} // namespace vultra
