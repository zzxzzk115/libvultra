#include "vultra/function/rendering/srp/builtin/features/test_feature.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"

namespace vultra
{
    void TestFeature::addPasses(FrameGraphBuildContext& ctx)
    {
        auto testOutput = m_TestPass.addPass(ctx);

        // Test
        ctx.data.set(kResKey_FinalCompositionTarget, testOutput);
    }
} // namespace vultra
