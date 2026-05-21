#pragma once

#include "vultra/function/rendering/srp/render_feature.hpp"

namespace vultra
{
    class FxaaPass;
    class HbaoPass;
    class IRenderService;
    class SsrPass;

    class BuiltinScreenSpaceFeature final : public RenderFeature
    {
    public:
        explicit BuiltinScreenSpaceFeature(IRenderService& renderService);
        ~BuiltinScreenSpaceFeature();

        DEFINE_RENDER_FEATURE(BuiltinScreenSpaceFeature);

        void addPasses(FrameGraphBuildContext& ctx) override;

    private:
        IRenderService& m_RenderService;
        HbaoPass*       m_HbaoPass {nullptr};
        SsrPass*        m_SsrPass {nullptr};
        FxaaPass*       m_FxaaPass {nullptr};
    };
} // namespace vultra
