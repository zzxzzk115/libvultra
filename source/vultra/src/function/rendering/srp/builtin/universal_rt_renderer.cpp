#include "vultra/function/rendering/srp/builtin/universal_rt_renderer.hpp"

#include "vultra/function/framegraph/framegraph_import.hpp"

namespace vultra
{
    void UniversalRtRenderer::buildFrameGraph(FrameGraphBuildContext& ctx)
    {
        const auto color = m_PrimaryPass.addPass(ctx);
        if (color)
        {
            const auto backBuffer = framegraph::importTexture(ctx.fg, "Backbuffer", ctx.view().target);
            m_FinalCompositionPass.compose(ctx, backBuffer);
        }
    }
} // namespace vultra
