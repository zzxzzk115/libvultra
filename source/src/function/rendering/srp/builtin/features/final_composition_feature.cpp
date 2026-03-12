#include "vultra/function/rendering/srp/builtin/features/final_composition_feature.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"
#include "vultra/function/framegraph/framegraph_import.hpp"
#include "vultra/function/rendering/srp/builtin/passes/final_composition_pass.hpp"

namespace vultra
{
    FinalCompositionFeature::FinalCompositionFeature() { m_FinalCompositionPass = new FinalCompositionPass(); }

    FinalCompositionFeature::~FinalCompositionFeature() { delete m_FinalCompositionPass; }

    void FinalCompositionFeature::addPasses(FrameGraphBuildContext& ctx)
    {
        const auto backBuffer = framegraph::importTexture(ctx.fg, "Backbuffer", ctx.view().target);
        m_FinalCompositionPass->compose(ctx, backBuffer);
    }
} // namespace vultra
