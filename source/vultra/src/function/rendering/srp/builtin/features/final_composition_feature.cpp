#include "vultra/function/rendering/srp/builtin/features/final_composition_feature.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"
#include "vultra/function/framegraph/framegraph_import.hpp"
#include "vultra/function/rendering/srp/builtin/passes/final_composition_pass.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"

namespace vultra
{
    FinalCompositionFeature::FinalCompositionFeature()
    {
        m_FinalCompositionPass = std::make_unique<FinalCompositionPass>();
    }

    FinalCompositionFeature::~FinalCompositionFeature() = default;

    void FinalCompositionFeature::addPasses(FrameGraphBuildContext& ctx)
    {
        if (!ctx.view().target || !ctx.data.contains(kResKey_FinalCompositionSource))
            return;

        const auto backBuffer =
            framegraph::importTexture(ctx.fg, "Backbuffer", ctx.view().target, ctx.view().renderTargetViewMask());
        m_FinalCompositionPass->compose(ctx, backBuffer);
    }
} // namespace vultra
