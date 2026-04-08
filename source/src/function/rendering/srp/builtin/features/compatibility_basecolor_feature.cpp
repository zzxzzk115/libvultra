#include "vultra/function/rendering/srp/builtin/features/compatibility_basecolor_feature.hpp"

#include "vultra/function/framegraph/framegraph_context.hpp"
#include "vultra/function/framegraph/framegraph_import.hpp"
#include "vultra/function/rendering/srp/builtin/passes/compatibility_basecolor_pass.hpp"

namespace vultra
{
    CompatibilityBaseColorFeature::CompatibilityBaseColorFeature()
    {
        m_BaseColorPass = new CompatibilityBaseColorPass();
    }

    CompatibilityBaseColorFeature::~CompatibilityBaseColorFeature() { delete m_BaseColorPass; }

    void CompatibilityBaseColorFeature::addPasses(FrameGraphBuildContext& ctx)
    {
        const bool hasMeshInstances = ctx.view().renderWorld != nullptr && !ctx.view().renderWorld->instances.empty();
        if (!hasMeshInstances || !ctx.view().target)
            return;

        const auto target = framegraph::importTexture(ctx.fg, "Backbuffer", ctx.view().target);
        m_BaseColorPass->addPass(ctx, target);
    }
} // namespace vultra
