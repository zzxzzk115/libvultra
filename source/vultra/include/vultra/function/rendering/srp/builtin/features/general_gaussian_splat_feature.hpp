#pragma once

#include "vultra/function/rendering/srp/render_feature.hpp"

namespace vultra
{
    class GeneralGaussianSplatPreprocessPass;
    class GeneralGaussianSplatRenderPass;
    class GeneralGaussianSplatFoveatedCompositePass;

    class GeneralGaussianSplatFeature final : public RenderFeature
    {
    public:
        GeneralGaussianSplatFeature();
        ~GeneralGaussianSplatFeature();

        DEFINE_RENDER_FEATURE(GeneralGaussianSplatFeature);

        void addPasses(FrameGraphBuildContext& ctx) override;

    private:
        GeneralGaussianSplatPreprocessPass*          m_PreprocessPass {nullptr};
        GeneralGaussianSplatRenderPass*              m_RenderPass {nullptr};
        GeneralGaussianSplatFoveatedCompositePass*   m_FoveatedCompositePass {nullptr};
    };
} // namespace vultra
