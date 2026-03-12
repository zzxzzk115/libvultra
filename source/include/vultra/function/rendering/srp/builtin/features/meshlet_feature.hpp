#pragma once

#include "vultra/function/rendering/srp/render_feature.hpp"

namespace vultra
{
    class MeshletCullPass;
    class BuildIndirectPass;

    class MeshletFeature final : public RenderFeature
    {
    public:
        MeshletFeature();
        ~MeshletFeature();

        DEFINE_RENDER_FEATURE(MeshletFeature);

        void addPasses(FrameGraphBuildContext& ctx) override;

    private:
        MeshletCullPass*   m_MeshletCullPass {nullptr};
        BuildIndirectPass* m_BuildIndirectPass {nullptr};
    };
} // namespace vultra
