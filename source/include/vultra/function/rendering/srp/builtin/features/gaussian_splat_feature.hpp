#pragma once

#include "vultra/function/rendering/srp/render_feature.hpp"

namespace vultra
{
    class GaussianSplatCullPass;
    class GaussianSplatDepthConsolidatePass;
    class GaussianSplatRenderPass;

    struct GaussianSplatRendererSettings
    {
        float frustumDilation {1.10f};
        float alphaCullThreshold {1.0f / 255.0f};
        float sizeCullingMinPixels {0.25f};
        float splatScale {1.0f};
        float maxAxisPixels {2048.0f};
        float depthIsoThreshold {0.7f};
        bool  enableExactDepthTransmittance {false};
        bool  enableXrViewReuse {false};
        bool  enableXrMultiview {false};
    };

    class GaussianSplatFeature final : public RenderFeature
    {
    public:
        GaussianSplatFeature();
        ~GaussianSplatFeature();

        DEFINE_RENDER_FEATURE(GaussianSplatFeature);

        void addPasses(FrameGraphBuildContext& ctx) override;

        GaussianSplatRendererSettings&       settings() { return m_Settings; }
        const GaussianSplatRendererSettings& settings() const { return m_Settings; }

    private:
        GaussianSplatCullPass*             m_CullPass {nullptr};
        GaussianSplatDepthConsolidatePass* m_DepthConsolidatePass {nullptr};
        GaussianSplatRenderPass*           m_RenderPass {nullptr};
        GaussianSplatRendererSettings      m_Settings {};
    };
} // namespace vultra
