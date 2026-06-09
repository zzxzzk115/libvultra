#pragma once

#include "vultra/function/rendering/srp/render_feature.hpp"

#include <memory>

namespace vultra
{
    class DeferredLightingPass;
    class DirectGBufferPass;
    class IRenderService;
    class ShadowMapPass;
    class SsaoPass;
    class SkyboxPass;

    class DirectGBufferFeature final : public RenderFeature
    {
    public:
        explicit DirectGBufferFeature(IRenderService& renderService);
        ~DirectGBufferFeature();

        DEFINE_RENDER_FEATURE(DirectGBufferFeature);

        void addPasses(FrameGraphBuildContext& ctx) override;

    private:
        IRenderService&                       m_RenderService;
        std::unique_ptr<DirectGBufferPass>    m_GBufferPass;
        std::unique_ptr<ShadowMapPass>        m_ShadowPass;
        std::unique_ptr<SsaoPass>             m_SsaoPass;
        std::unique_ptr<DeferredLightingPass> m_LightingPass;
        std::unique_ptr<SkyboxPass>           m_SkyboxPass;
    };
} // namespace vultra
