#pragma once

#include "vultra/function/rendering/srp/render_feature.hpp"

#include <memory>

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
        std::unique_ptr<CompatibilityBaseColorPass> m_BaseColorPass;
    };
} // namespace vultra
