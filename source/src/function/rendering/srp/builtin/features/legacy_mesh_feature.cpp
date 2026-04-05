#include "vultra/function/rendering/srp/builtin/features/legacy_mesh_feature.hpp"

#include "vultra/function/framegraph/framegraph_import.hpp"
#include "vultra/function/rendering/srp/builtin/passes/legacy_basecolor_pass.hpp"
#include "vultra/function/rendering/srp/builtin/resource_keys.hpp"

namespace vultra
{
    LegacyMeshFeature::LegacyMeshFeature(const LegacyRendererProfile profile) : m_Profile(profile)
    {
        m_LegacyBaseColorPass = new LegacyBaseColorPass(profile);
    }

    LegacyMeshFeature::~LegacyMeshFeature() { delete m_LegacyBaseColorPass; }

    void LegacyMeshFeature::addPasses(FrameGraphBuildContext& ctx)
    {
        if (m_Profile == LegacyRendererProfile::eWebGPUCompat)
        {
            const auto backBuffer = framegraph::importTexture(ctx.fg, "Backbuffer", ctx.view().target);
            m_LegacyBaseColorPass->addPass(ctx, backBuffer);
            return;
        }

        const auto color = m_LegacyBaseColorPass->addPass(ctx);
        ctx.data.set(kResKey_FinalCompositionSource, color);
    }
} // namespace vultra

