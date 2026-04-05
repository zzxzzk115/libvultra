#pragma once

#include "vultra/function/rendering/srp/builtin/legacy_renderer_profile.hpp"
#include "vultra/function/rendering/srp/renderer.hpp"

#include <string>

namespace vultra
{
    class LegacyRenderer : public FeatureRenderer
    {
    public:
        LegacyRenderer(std::string name, LegacyRendererProfile profile);

        std::string_view name() const override { return m_Name; }

        void init() override;
        void render(ImmediateRenderContext& ctx) override;
        void onImGui() override;

    private:
        std::string           m_Name;
        LegacyRendererProfile m_Profile;
        bool                  m_FeaturesInitialized {false};
    };
} // namespace vultra

