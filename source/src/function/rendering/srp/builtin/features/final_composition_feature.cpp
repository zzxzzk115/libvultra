#include "vultra/function/rendering/srp/builtin/features/final_composition_feature.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"

namespace vultra
{
    void FinalCompositionFeature::addPasses(FrameGraphBuildContext& ctx)
    {
        m_FinalCompositionPass.compose(ctx, ctx.data.get(kResKey_FinalCompositionTarget));
    }
} // namespace vultra
