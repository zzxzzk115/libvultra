#pragma once

#include "vultra/function/rendering/srp/render_feature.hpp"

namespace vultra
{
    class FxaaPass;
    class SsaoPass;
    class IRenderService;
    class SelectionOutlinePass;
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
        SsaoPass*       m_SsaoPass {nullptr};
        SsrPass*        m_SsrPass {nullptr};
        SelectionOutlinePass* m_SelectionOutlinePass {nullptr};
        FxaaPass*       m_FxaaPass {nullptr};
    };
} // namespace vultra
