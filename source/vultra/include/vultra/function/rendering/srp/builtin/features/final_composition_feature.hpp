#pragma once

#include "vultra/function/rendering/srp/render_feature.hpp"

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
        FinalCompositionPass* m_FinalCompositionPass {nullptr};
    };
} // namespace vultra
