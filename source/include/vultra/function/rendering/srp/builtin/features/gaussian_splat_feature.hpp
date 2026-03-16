#pragma once

#include "vultra/function/rendering/srp/render_feature.hpp"

namespace vultra
{
    class GaussianSplatCullPass;
    class GaussianSplatRenderPass;

    class GaussianSplatFeature final : public RenderFeature
    {
    public:
        GaussianSplatFeature();
        ~GaussianSplatFeature();

        DEFINE_RENDER_FEATURE(GaussianSplatFeature);

        void addPasses(FrameGraphBuildContext& ctx) override;

    private:
        GaussianSplatCullPass*   m_CullPass {nullptr};
        GaussianSplatRenderPass* m_RenderPass {nullptr};
    };
} // namespace vultra
