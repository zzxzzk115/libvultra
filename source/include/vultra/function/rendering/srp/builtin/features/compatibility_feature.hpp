#pragma once

#include "vultra/function/rendering/srp/render_feature.hpp"

namespace vultra
{
    class CompatibilityBaseColorPass;

    class CompatibilityFeature final : public RenderFeature
    {
    public:
        CompatibilityFeature();
        ~CompatibilityFeature();

        DEFINE_RENDER_FEATURE(CompatibilityFeature);

        void addPasses(FrameGraphBuildContext& ctx) override;

    private:
        CompatibilityBaseColorPass* m_CompatibilityBaseColorPass {nullptr};
    };
} // namespace vultra

