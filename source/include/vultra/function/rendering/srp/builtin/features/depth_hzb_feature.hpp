#pragma once

#include "vultra/function/rendering/srp/render_feature.hpp"

namespace vultra
{
    class DepthPrePass;
    class HzbGeneratePass;

    class DepthHzbFeature final : public RenderFeature
    {
    public:
        DepthHzbFeature();
        ~DepthHzbFeature();

        DEFINE_RENDER_FEATURE(DepthHzbFeature);

        void addPasses(FrameGraphBuildContext& ctx) override;

    private:
        DepthPrePass*    m_DepthPrePass {nullptr};
        HzbGeneratePass* m_HzbGeneratePass {nullptr};
    };
} // namespace vultra
