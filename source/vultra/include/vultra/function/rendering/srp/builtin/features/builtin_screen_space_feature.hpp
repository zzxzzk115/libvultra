#pragma once

#include "vultra/function/rendering/srp/render_feature.hpp"

#include <memory>

namespace vultra
{
    class FxaaPass;
    class IRenderService;
    class SelectionOutlinePass;
    class SsrCompositePass;
    class SsrPass;
    class ToneMappingPass;
    class UiOverlayPass;

    class BuiltinScreenSpaceFeature final : public RenderFeature
    {
    public:
        explicit BuiltinScreenSpaceFeature(IRenderService& renderService);
        ~BuiltinScreenSpaceFeature();

        DEFINE_RENDER_FEATURE(BuiltinScreenSpaceFeature);

        void addPasses(FrameGraphBuildContext& ctx) override;

    private:
        IRenderService&                       m_RenderService;
        std::unique_ptr<SsrPass>              m_SsrPass;
        std::unique_ptr<SsrCompositePass>     m_SsrCompositePass;
        std::unique_ptr<ToneMappingPass>      m_ToneMappingPass;
        std::unique_ptr<SelectionOutlinePass> m_SelectionOutlinePass;
        std::unique_ptr<FxaaPass>             m_FxaaPass;
        std::unique_ptr<UiOverlayPass>        m_UiOverlayPass;
    };
} // namespace vultra
