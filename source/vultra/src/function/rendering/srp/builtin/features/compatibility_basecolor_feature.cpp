#include "vultra/function/rendering/srp/builtin/features/compatibility_basecolor_feature.hpp"

#include "vultra/function/framegraph/framegraph_context.hpp"
#include "vultra/function/rendering/srp/builtin/passes/compatibility_basecolor_pass.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"

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

        auto color = m_BaseColorPass->addPass(ctx);
        if (color)
            ctx.data.set(kResKey_FinalCompositionSource, color);
    }
} // namespace vultra
