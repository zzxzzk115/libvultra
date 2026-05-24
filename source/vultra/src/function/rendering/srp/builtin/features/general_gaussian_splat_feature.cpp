#include "vultra/function/rendering/srp/builtin/features/general_gaussian_splat_feature.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"
#include "vultra/function/rendering/srp/builtin/passes/general_gaussian_splat_foveated_composite_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/general_gaussian_splat_preprocess_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/general_gaussian_splat_render_pass.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"

#include <algorithm>
#include <cmath>

namespace vultra
{
    namespace
    {
        [[nodiscard]] rhi::Extent2D scaleExtent(const rhi::Extent2D extent, const float scale)
        {
            const float safeScale = std::clamp(scale, 0.05f, 1.0f);
            return {
                .width  = std::max(1u, static_cast<uint32_t>(std::ceil(static_cast<float>(extent.width) * safeScale))),
                .height = std::max(1u, static_cast<uint32_t>(std::ceil(static_cast<float>(extent.height) * safeScale))),
            };
        }
    } // namespace

    GeneralGaussianSplatFeature::GeneralGaussianSplatFeature()
    {
        m_PreprocessPass        = new GeneralGaussianSplatPreprocessPass();
        m_RenderPass            = new GeneralGaussianSplatRenderPass();
        m_FoveatedCompositePass = new GeneralGaussianSplatFoveatedCompositePass();
    }

    GeneralGaussianSplatFeature::~GeneralGaussianSplatFeature()
    {
        delete m_FoveatedCompositePass;
        delete m_RenderPass;
        delete m_PreprocessPass;
    }

    void GeneralGaussianSplatFeature::addPasses(FrameGraphBuildContext& ctx)
    {
        auto* gpuSceneView = ctx.view().gpuSceneView;
        if (!gpuSceneView || !gpuSceneView->hasGeneralGaussianSplats())
            return;

        auto setFinalColor = [&](FrameGraphResource color) {
            if (gpuSceneView->generalGaussianSplatFoveatedDebugOverlayEnabled && color)
            {
                if (auto overlay = m_FoveatedCompositePass->debugOverlay(ctx, color))
                    color = overlay;
            }
            if (color)
                ctx.data.set(kResKey_FinalCompositionSource, color);
        };

        m_PreprocessPass->addPass(ctx);
        if (gpuSceneView->generalGaussianSplatFoveatedLayeredCompositeEnabled)
        {
            const auto baseColor = ctx.data.tryGet(kResKey_FinalCompositionSource);
            const auto fovea = m_RenderPass->addFoveatedLayerPass(ctx,
                                                                  GeneralGaussianSplatFoveatedLayer::eFovea,
                                                                  scaleExtent(ctx.view().extent,
                                                                              gpuSceneView->generalGaussianSplatFoveatedResolutionScales.x));
            const auto mid   = m_RenderPass->addFoveatedLayerPass(ctx,
                                                                  GeneralGaussianSplatFoveatedLayer::eMid,
                                                                  scaleExtent(ctx.view().extent,
                                                                              gpuSceneView->generalGaussianSplatFoveatedResolutionScales.y));
            const auto outer = m_RenderPass->addFoveatedLayerPass(ctx,
                                                                  GeneralGaussianSplatFoveatedLayer::eOuter,
                                                                  scaleExtent(ctx.view().extent,
                                                                              gpuSceneView->generalGaussianSplatFoveatedResolutionScales.z));
            auto color = m_FoveatedCompositePass->compose(ctx, fovea, mid, outer, baseColor);
            setFinalColor(color);
            return;
        }

        auto color = m_RenderPass->addPass(ctx);
        setFinalColor(color);
    }
} // namespace vultra
