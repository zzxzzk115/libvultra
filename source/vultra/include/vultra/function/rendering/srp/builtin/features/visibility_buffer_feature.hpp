#pragma once

#include "vultra/function/rendering/srp/render_feature.hpp"

namespace vultra
{
    class ThinGBufferPass;
    class VisibilityBufferPass;

    class VisibilityBufferFeature final : public RenderFeature
    {
    public:
        VisibilityBufferFeature();
        ~VisibilityBufferFeature();

        DEFINE_RENDER_FEATURE(VisibilityBufferFeature);

        void addPasses(FrameGraphBuildContext& ctx) override;

    private:
        VisibilityBufferPass* m_VisibilityBufferPass {nullptr};
        ThinGBufferPass*      m_ThinGBufferPass {nullptr};
    };
} // namespace vultra
