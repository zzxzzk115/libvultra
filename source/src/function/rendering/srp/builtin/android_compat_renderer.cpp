#include "vultra/function/rendering/srp/builtin/android_compat_renderer.hpp"

#include "vultra/function/rendering/srp/builtin/features/android_mesh_feature.hpp"
#include "vultra/function/rendering/srp/builtin/features/final_composition_feature.hpp"

namespace vultra
{
    void AndroidCompatRenderer::init()
    {
        if (m_FeaturesInitialized)
            return;
        emplaceFeature<AndroidMeshFeature>();
        emplaceFeature<FinalCompositionFeature>();
        m_FeaturesInitialized = true;
    }

    void AndroidCompatRenderer::render(ImmediateRenderContext& ctx)
    {
        ctx.clear();
    }

    void AndroidCompatRenderer::onImGui()
    {
        // Intentionally minimal for now.
    }
} // namespace vultra
