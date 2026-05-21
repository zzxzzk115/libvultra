#include "vultra/function/rendering/srp/builtin/features/visibility_buffer_feature.hpp"

#include "vultra/function/framegraph/framegraph_context.hpp"
#include "vultra/function/rendering/srp/builtin/passes/thin_gbuffer_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/visibility_buffer_pass.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"

namespace vultra
{
    VisibilityBufferFeature::VisibilityBufferFeature()
    {
        m_VisibilityBufferPass = new VisibilityBufferPass();
        m_ThinGBufferPass      = new ThinGBufferPass();
    }

    VisibilityBufferFeature::~VisibilityBufferFeature()
    {
        delete m_VisibilityBufferPass;
        delete m_ThinGBufferPass;
    }

    void VisibilityBufferFeature::addPasses(FrameGraphBuildContext& ctx)
    {
        auto* gpuSceneView = ctx.view().gpuSceneView;
        if (!gpuSceneView)
            return;

        const bool hasMeshletDraws =
            gpuSceneView->isGpuDriven() ? (gpuSceneView->maxDraws > 0u) : (gpuSceneView->countMeshletDraws() > 0u);
        const bool hasGeneralGaussianSplats = gpuSceneView->hasGeneralGaussianSplats();

        if (!hasMeshletDraws)
        {
            // Gaussian-only views should let the splat feature allocate its own color target.
            if (hasGeneralGaussianSplats)
                return;

            auto visibility = m_VisibilityBufferPass->addPass(ctx);
            auto color      = m_ThinGBufferPass->addPass(ctx, visibility);
            ctx.data.set(kResKey_FinalCompositionSource, color);
            return;
        }

        auto visibility = m_VisibilityBufferPass->addPass(ctx);
        auto color      = m_ThinGBufferPass->addPass(ctx, visibility);
        ctx.data.set(kResKey_FinalCompositionSource, color);
    }
} // namespace vultra
