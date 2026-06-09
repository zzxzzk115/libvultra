#pragma once

#include "vultra/function/rendering/srp/render_feature.hpp"

#include <memory>

namespace vultra
{
    class FinalCompositionPass;

    class FinalCompositionFeature : public RenderFeature
    {
    public:
        FinalCompositionFeature();
        ~FinalCompositionFeature();

        DEFINE_RENDER_FEATURE(FinalCompositionFeature);

        void addPasses(FrameGraphBuildContext& ctx) override;

    private:
        std::unique_ptr<FinalCompositionPass> m_FinalCompositionPass;
    };
} // namespace vultra
