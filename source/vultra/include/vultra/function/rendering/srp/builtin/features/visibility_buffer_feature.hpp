#pragma once

#include "vultra/function/rendering/srp/render_feature.hpp"

#include <memory>

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
        std::unique_ptr<VisibilityBufferPass> m_VisibilityBufferPass;
        std::unique_ptr<ThinGBufferPass>      m_ThinGBufferPass;
    };
} // namespace vultra
