#pragma once

#include "vultra/function/rendering/srp/render_feature.hpp"

namespace vultra
{
    class DeferredLightingPass;
    class DirectGBufferPass;
    class IRenderService;
    class ShadowMapPass;

    class DirectGBufferFeature final : public RenderFeature
    {
    public:
        explicit DirectGBufferFeature(IRenderService& renderService);
        ~DirectGBufferFeature();

        DEFINE_RENDER_FEATURE(DirectGBufferFeature);

        void addPasses(FrameGraphBuildContext& ctx) override;

    private:
        IRenderService&        m_RenderService;
        DirectGBufferPass*     m_GBufferPass {nullptr};
        ShadowMapPass*         m_ShadowPass {nullptr};
        DeferredLightingPass*  m_LightingPass {nullptr};
    };
} // namespace vultra
