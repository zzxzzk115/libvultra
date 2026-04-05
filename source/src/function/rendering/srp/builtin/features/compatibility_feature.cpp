#include "vultra/function/rendering/srp/builtin/features/compatibility_feature.hpp"

#include "vultra/core/rhi/structs/render_backend_api.hpp"
#include "vultra/function/framegraph/framegraph_import.hpp"
#include "vultra/function/rendering/srp/builtin/passes/compatibility_basecolor_pass.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"

namespace vultra
{
    CompatibilityFeature::CompatibilityFeature() { m_CompatibilityBaseColorPass = new CompatibilityBaseColorPass(); }

    CompatibilityFeature::~CompatibilityFeature() { delete m_CompatibilityBaseColorPass; }

    void CompatibilityFeature::addPasses(FrameGraphBuildContext& ctx)
    {
        if (ctx.rd.getBackendApi() == rhi::RenderBackendApi::eWebGPU)
        {
            const auto backBuffer = framegraph::importTexture(ctx.fg, "Backbuffer", ctx.view().target);
            m_CompatibilityBaseColorPass->addPass(ctx, backBuffer);
            return;
        }

        const auto color = m_CompatibilityBaseColorPass->addPass(ctx);
        ctx.data.set(kResKey_FinalCompositionSource, color);
    }
} // namespace vultra

