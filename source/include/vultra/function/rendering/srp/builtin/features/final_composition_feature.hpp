#pragma once

#include "vultra/function/rendering/srp/builtin/passes/final_composition_pass.hpp"
#include "vultra/function/rendering/srp/render_feature.hpp"

namespace vultra
{
    class FinalCompositionFeature : public RenderFeature
    {
    public:
        DEFINE_RENDER_FEATURE(FinalCompositionFeature);

        virtual void addPasses(FrameGraphBuildContext& ctx) override;

    private:
        FinalCompositionPass m_FinalCompositionPass;
    };
} // namespace vultra