#include "vultra/function/rendering/srp/builtin/features/general_gaussian_splat_feature.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"
#include "vultra/function/rendering/srp/builtin/passes/general_gaussian_splat_preprocess_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/general_gaussian_splat_render_pass.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"

namespace vultra
{
    GeneralGaussianSplatFeature::GeneralGaussianSplatFeature()
    {
        m_PreprocessPass = new GeneralGaussianSplatPreprocessPass();
        m_RenderPass     = new GeneralGaussianSplatRenderPass();
    }

    GeneralGaussianSplatFeature::~GeneralGaussianSplatFeature()
    {
        delete m_RenderPass;
        delete m_PreprocessPass;
    }

    void GeneralGaussianSplatFeature::addPasses(FrameGraphBuildContext& ctx)
    {
        auto* gpuSceneView = ctx.view().gpuSceneView;
        if (!gpuSceneView || !gpuSceneView->hasGeneralGaussianSplats())
            return;

        m_PreprocessPass->addPass(ctx);
        auto color = m_RenderPass->addPass(ctx);
        if (color)
            ctx.data.set(kResKey_FinalCompositionSource, color);
    }
} // namespace vultra
