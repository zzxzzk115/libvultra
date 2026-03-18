#include "vultra/function/rendering/srp/builtin/features/test_feature.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"
#include "vultra/function/rendering/srp/builtin/passes/splat_composite_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/test_pass.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"

namespace vultra
{
    TestFeature::TestFeature()
    {
        m_TestPass           = new TestPass();
        m_SplatCompositePass = new SplatCompositePass();
    }

    TestFeature::~TestFeature()
    {
        delete m_TestPass;
        delete m_SplatCompositePass;
    }

    void TestFeature::addPasses(FrameGraphBuildContext& ctx)
    {
        auto*      gpuSceneView = ctx.view().gpuSceneView;
        const bool hasMeshletDraws =
            gpuSceneView &&
            (gpuSceneView->isGpuDriven() ? (gpuSceneView->maxDraws > 0u) : (gpuSceneView->countMeshletDraws() > 0u));

        if (!hasMeshletDraws)
        {
            if (ctx.data.contains(kResKey_GaussianSplatRenderDone))
            {
                ctx.data.set(kResKey_FinalCompositionSource, ctx.data.get(kResKey_GaussianSplatRenderDone));
            }
            else
            {
                auto fallbackColor = m_TestPass->addPass(ctx);
                ctx.data.set(kResKey_FinalCompositionSource, fallbackColor);
            }
            return;
        }

        auto meshletColor = m_TestPass->addPass(ctx);

        // If GaussianSplatFeature ran before us, blend its output on top.
        if (ctx.data.contains(kResKey_GaussianSplatRenderDone))
        {
            auto splatColor     = ctx.data.get(kResKey_GaussianSplatRenderDone);
            auto compositeColor = m_SplatCompositePass->addPass(ctx, meshletColor, splatColor);
            ctx.data.set(kResKey_FinalCompositionSource, compositeColor);
        }
        else
        {
            ctx.data.set(kResKey_FinalCompositionSource, meshletColor);
        }
    }
} // namespace vultra
