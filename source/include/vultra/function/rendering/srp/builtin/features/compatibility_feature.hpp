#pragma once

#include "vultra/function/rendering/srp/builtin/features/gaussian_splat_feature.hpp"
#include "vultra/function/rendering/srp/render_feature.hpp"

namespace vultra
{
    class CompatibilityBaseColorPass;
    class CompatibilityGaussianSplatCullPass;
    class CompatibilityGaussianSplatRenderPass;

    class CompatibilityFeature final : public RenderFeature
    {
    public:
        CompatibilityFeature();
        ~CompatibilityFeature();

        DEFINE_RENDER_FEATURE(CompatibilityFeature);

        void addPasses(FrameGraphBuildContext& ctx) override;

        GaussianSplatRendererSettings&       gaussianSplatSettings() { return m_GaussianSplatSettings; }
        const GaussianSplatRendererSettings& gaussianSplatSettings() const { return m_GaussianSplatSettings; }

    private:
        CompatibilityBaseColorPass*          m_CompatibilityBaseColorPass {nullptr};
        CompatibilityGaussianSplatCullPass*  m_GaussianSplatCullPass {nullptr};
        CompatibilityGaussianSplatRenderPass* m_GaussianSplatRenderPass {nullptr};
        GaussianSplatRendererSettings        m_GaussianSplatSettings {};
    };
} // namespace vultra
