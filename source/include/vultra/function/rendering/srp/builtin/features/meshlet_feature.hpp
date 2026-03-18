#pragma once

#include "vultra/function/rendering/srp/render_feature.hpp"

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
        CoarseInstanceCullPass* m_CoarseInstanceCullPass {nullptr};
        MeshletCullPass*        m_MeshletCullPass {nullptr};
        BuildIndirectPass*      m_BuildIndirectPass {nullptr};
        DrawsetBuildPass*       m_DrawsetBuildPass {nullptr};
        DepthPrePass*           m_DepthPrePass {nullptr};
    };
} // namespace vultra
