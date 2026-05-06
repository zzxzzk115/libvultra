#pragma once

#include "vultra/function/rendering/srp/render_feature.hpp"

namespace vultra
{
    class CompatibilityBaseColorPass;

    class CompatibilityBaseColorFeature final : public RenderFeature
    {
    public:
        CompatibilityBaseColorFeature();
        ~CompatibilityBaseColorFeature();

        DEFINE_RENDER_FEATURE(CompatibilityBaseColorFeature);

        void addPasses(FrameGraphBuildContext& ctx) override;

    private:
        CompatibilityBaseColorPass* m_BaseColorPass {nullptr};
    };
} // namespace vultra
