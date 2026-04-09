#include "vultra/function/rendering/srp/builtin/features/test_feature.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"
#include "vultra/function/rendering/srp/builtin/passes/test_pass.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"

namespace vultra
{
    TestFeature::TestFeature()
    {
        m_TestPass = new TestPass();
    }

    TestFeature::~TestFeature() { delete m_TestPass; }

    void TestFeature::addPasses(FrameGraphBuildContext& ctx)
    {
        auto*      gpuSceneView = ctx.view().gpuSceneView;
        const bool hasMeshletDraws =
            gpuSceneView &&
            (gpuSceneView->isGpuDriven() ? (gpuSceneView->maxDraws > 0u) : (gpuSceneView->countMeshletDraws() > 0u));
        const bool hasGeneralGaussianSplats = gpuSceneView && gpuSceneView->hasGeneralGaussianSplats();

        if (!hasMeshletDraws)
        {
            // Let gaussian-only views allocate their own color target so XR multiview keeps a layered attachment.
            if (hasGeneralGaussianSplats)
                return;

            auto fallbackColor = m_TestPass->addPass(ctx);
            ctx.data.set(kResKey_FinalCompositionSource, fallbackColor);
            return;
        }

        auto meshletColor = m_TestPass->addPass(ctx);
        ctx.data.set(kResKey_FinalCompositionSource, meshletColor);
    }
} // namespace vultra
