#include "vultra/function/rendering/srp/builtin/features/compatibility_feature.hpp"

#include "vultra/core/rhi/structs/render_backend_api.hpp"
#include "vultra/function/framegraph/framegraph_import.hpp"
#include "vultra/function/rendering/srp/builtin/passes/compatibility_basecolor_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/compatibility_gaussian_splat_cull_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/compatibility_gaussian_splat_render_pass.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"

namespace vultra
{
    CompatibilityFeature::CompatibilityFeature()
    {
        m_CompatibilityBaseColorPass = new CompatibilityBaseColorPass();
        m_GaussianSplatCullPass      = new CompatibilityGaussianSplatCullPass();
        m_GaussianSplatRenderPass    = new CompatibilityGaussianSplatRenderPass();
    }

    CompatibilityFeature::~CompatibilityFeature()
    {
        delete m_GaussianSplatRenderPass;
        delete m_GaussianSplatCullPass;
        delete m_CompatibilityBaseColorPass;
    }

    void CompatibilityFeature::addPasses(FrameGraphBuildContext& ctx)
    {
        if (ctx.rd.getBackendApi() == rhi::RenderBackendApi::eWebGPU)
        {
            const auto backBuffer = framegraph::importTexture(ctx.fg, "Backbuffer", ctx.view().target);
            m_CompatibilityBaseColorPass->addPass(ctx, backBuffer);
            const auto buildToken = m_GaussianSplatCullPass->addPass(ctx, {}, m_GaussianSplatSettings);
            m_GaussianSplatRenderPass->addPass(ctx, buildToken, backBuffer, m_GaussianSplatSettings);
            return;
        }

        const auto color = m_CompatibilityBaseColorPass->addPass(ctx);
        const auto buildToken = m_GaussianSplatCullPass->addPass(ctx, {}, m_GaussianSplatSettings);
        const auto composed   = m_GaussianSplatRenderPass->addPass(ctx, buildToken, color, m_GaussianSplatSettings);
        ctx.data.set(kResKey_FinalCompositionSource, composed);
    }
} // namespace vultra
