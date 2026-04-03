#pragma once

#include "vultra/function/rendering/srp/render_feature.hpp"

namespace vultra
{
    class WebGPUBaseColorPass;

    class WebGPUMeshFeature final : public RenderFeature
    {
    public:
        WebGPUMeshFeature();
        ~WebGPUMeshFeature();

        DEFINE_RENDER_FEATURE(WebGPUMeshFeature);

        void addPasses(FrameGraphBuildContext& ctx) override;

    private:
        WebGPUBaseColorPass* m_WebGPUBaseColorPass {nullptr};
    };
} // namespace vultra
