#pragma once

#include "vultra/function/rendering/srp/renderer.hpp"

namespace vultra
{
    class WebGPUCompatRenderer final : public FeatureRenderer
    {
    public:
        std::string_view name() const override { return "webgpu_compat"; }

        void init() override;
        void render(ImmediateRenderContext& ctx) override;
        void onImGui() override;

    private:
        bool m_FeaturesInitialized {false};
    };
} // namespace vultra
