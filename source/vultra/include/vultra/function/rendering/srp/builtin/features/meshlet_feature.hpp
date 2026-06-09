#pragma once

#include "vultra/function/rendering/srp/render_feature.hpp"

#include <memory>

namespace vultra
{
    class CoarseInstanceCullPass;
    class MeshletCullPass;
    class BuildIndirectPass;
    class DrawsetBuildPass;
    class DepthPrePass;

    class MeshletFeature final : public RenderFeature
    {
    public:
        MeshletFeature();
        ~MeshletFeature();

        DEFINE_RENDER_FEATURE(MeshletFeature);

        void addPasses(FrameGraphBuildContext& ctx) override;

    private:
        std::unique_ptr<CoarseInstanceCullPass> m_CoarseInstanceCullPass;
        std::unique_ptr<MeshletCullPass>        m_MeshletCullPass;
        std::unique_ptr<BuildIndirectPass>      m_BuildIndirectPass;
        std::unique_ptr<DrawsetBuildPass>       m_DrawsetBuildPass;
        std::unique_ptr<DepthPrePass>           m_DepthPrePass;
    };
} // namespace vultra
