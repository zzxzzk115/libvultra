#pragma once

#include "vultra/function/rendering/srp/render_feature.hpp"

namespace vultra
{
    class FxaaPass;
    class IRenderService;
    class SelectionOutlinePass;
    class SsrCompositePass;
    class SsrPass;
    class ToneMappingPass;

    class BuiltinScreenSpaceFeature final : public RenderFeature
    {
    public:
        explicit BuiltinScreenSpaceFeature(IRenderService& renderService);
        ~BuiltinScreenSpaceFeature();

        DEFINE_RENDER_FEATURE(BuiltinScreenSpaceFeature);

        void addPasses(FrameGraphBuildContext& ctx) override;

    private:
        IRenderService& m_RenderService;
        SsrPass*        m_SsrPass {nullptr};
        SsrCompositePass* m_SsrCompositePass {nullptr};
        ToneMappingPass*  m_ToneMappingPass {nullptr};
        SelectionOutlinePass* m_SelectionOutlinePass {nullptr};
        FxaaPass*       m_FxaaPass {nullptr};
    };
} // namespace vultra
