#include "vultra/function/rendering/srp/builtin/universal_rt_renderer.hpp"

#include "vultra/function/framegraph/framegraph_import.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"

namespace vultra
{
    void UniversalRtRenderer::buildFrameGraph(FrameGraphBuildContext& ctx)
    {
        const auto color = m_PrimaryPass.addPass(ctx);
        if (color)
        {
            const auto toneMapped = m_ToneMappingPass.addPass(ctx, color);
            if (toneMapped)
                ctx.data.set(kResKey_FinalCompositionSource, toneMapped);
            const auto backBuffer =
                framegraph::importTexture(ctx.fg, "Backbuffer", ctx.view().target, ctx.view().renderTargetViewMask());
            m_FinalCompositionPass.compose(ctx, backBuffer);
        }
    }
} // namespace vultra
