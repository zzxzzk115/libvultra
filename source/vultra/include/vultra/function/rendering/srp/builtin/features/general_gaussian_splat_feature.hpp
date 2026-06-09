#pragma once

#include "vultra/function/rendering/srp/render_feature.hpp"

#include <memory>

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
        std::unique_ptr<GeneralGaussianSplatPreprocessPass>        m_PreprocessPass;
        std::unique_ptr<GeneralGaussianSplatRenderPass>           m_RenderPass;
        std::unique_ptr<GeneralGaussianSplatFoveatedCompositePass> m_FoveatedCompositePass;
    };
} // namespace vultra
