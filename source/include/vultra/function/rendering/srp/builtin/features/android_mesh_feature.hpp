#pragma once

#include "vultra/function/rendering/srp/render_feature.hpp"

namespace vultra
{
    class AndroidBaseColorPass;

    class AndroidMeshFeature final : public RenderFeature
    {
    public:
        AndroidMeshFeature();
        ~AndroidMeshFeature();

        DEFINE_RENDER_FEATURE(AndroidMeshFeature);

        void addPasses(FrameGraphBuildContext& ctx) override;

    private:
        AndroidBaseColorPass* m_AndroidBaseColorPass {nullptr};
    };
} // namespace vultra
