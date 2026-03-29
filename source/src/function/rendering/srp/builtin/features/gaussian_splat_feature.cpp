#include "vultra/function/rendering/srp/builtin/features/gaussian_splat_feature.hpp"

#include "vultra/function/rendering/srp/builtin/passes/gaussian_splat_cull_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/gaussian_splat_depth_consolidate_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/gaussian_splat_render_pass.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"

namespace vultra
{
    GaussianSplatFeature::GaussianSplatFeature()
    {
        m_CullPass             = new GaussianSplatCullPass();
        m_DepthConsolidatePass = new GaussianSplatDepthConsolidatePass();
        m_RenderPass           = new GaussianSplatRenderPass();
    }

    GaussianSplatFeature::~GaussianSplatFeature()
    {
        delete m_CullPass;
        delete m_DepthConsolidatePass;
        delete m_RenderPass;
    }

    void GaussianSplatFeature::addPasses(FrameGraphBuildContext& ctx)
    {
        auto*      gpuSceneView = ctx.view().gpuSceneView;
        const bool hasMeshletDraws =
            gpuSceneView &&
            (gpuSceneView->isGpuDriven() ? (gpuSceneView->maxDraws > 0u) : (gpuSceneView->countMeshletDraws() > 0u));

        const auto* camera              = ctx.view().camera;
        const bool  reusePreviousXrCull = m_Settings.enableXrViewReuse && camera && camera->isXRView &&
                                         camera->viewCount > 1u && camera->viewIndex > 0u;

        auto cullDone   = reusePreviousXrCull ? FrameGraphResource {} : m_CullPass->addPass(ctx, {}, m_Settings);
        auto renderDone = m_RenderPass->addPass(ctx, cullDone, m_Settings, hasMeshletDraws);
        if (ctx.data.contains(kResKey_GaussianSplatDepthTransmittance))
            m_DepthConsolidatePass->addPass(ctx, ctx.data.get(kResKey_GaussianSplatDepthTransmittance));

        ctx.data.set(kResKey_GaussianSplatCullDone, cullDone);
        ctx.data.set(kResKey_GaussianSplatRenderDone, renderDone);
        ctx.data.set(kResKey_FinalCompositionSource, renderDone);
    }
} // namespace vultra
