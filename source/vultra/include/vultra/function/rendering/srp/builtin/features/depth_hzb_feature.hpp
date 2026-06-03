#pragma once

#include "vultra/function/rendering/srp/render_feature.hpp"

#include <memory>

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
        std::unique_ptr<DepthPrePass>    m_DepthPrePass;
        std::unique_ptr<HzbGeneratePass> m_HzbGeneratePass;
    };
} // namespace vultra
