#include "vultra/function/rendering/srp/builtin/features/gaussian_splat_feature.hpp"

#include "vultra/function/rendering/srp/builtin/passes/gaussian_splat_cull_pass.hpp"
#include "vultra/function/rendering/srp/builtin/passes/gaussian_splat_render_pass.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"

namespace vultra
{
    GaussianSplatFeature::GaussianSplatFeature()
    {
        m_CullPass = new GaussianSplatCullPass();
        m_RenderPass        = new GaussianSplatRenderPass();
    }

    GaussianSplatFeature::~GaussianSplatFeature()
    {
        delete m_CullPass;
        delete m_RenderPass;
    }

    void GaussianSplatFeature::addPasses(FrameGraphBuildContext& ctx)
    {
        auto cullDone   = m_CullPass->addPass(ctx, {});
        auto renderDone = m_RenderPass->addPass(ctx, cullDone);

        ctx.data.set(kResKey_GaussianSplatCullDone, cullDone);
        ctx.data.set(kResKey_GaussianSplatRenderDone, renderDone);
        ctx.data.set(kResKey_FinalCompositionSource, renderDone);
    }
} // namespace vultra
