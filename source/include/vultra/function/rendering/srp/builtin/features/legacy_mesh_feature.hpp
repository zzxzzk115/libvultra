#pragma once

#include "vultra/function/rendering/srp/builtin/legacy_renderer_profile.hpp"
#include "vultra/function/rendering/srp/render_feature.hpp"

namespace vultra
{
    class LegacyBaseColorPass;

    class LegacyMeshFeature final : public RenderFeature
    {
    public:
        explicit LegacyMeshFeature(LegacyRendererProfile profile);
        ~LegacyMeshFeature();

        DEFINE_RENDER_FEATURE(LegacyMeshFeature);

        void addPasses(FrameGraphBuildContext& ctx) override;

    private:
        LegacyRendererProfile m_Profile;
        LegacyBaseColorPass*  m_LegacyBaseColorPass {nullptr};
    };
} // namespace vultra

