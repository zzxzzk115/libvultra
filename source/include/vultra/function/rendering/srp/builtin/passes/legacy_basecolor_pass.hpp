#pragma once

#include "vultra/function/framegraph/framegraph_context.hpp"
#include "vultra/function/rendering/srp/builtin/legacy_renderer_profile.hpp"

namespace vultra
{
    class AndroidBaseColorPass;
    class WebGPUBaseColorPass;

    class LegacyBaseColorPass final
    {
    public:
        explicit LegacyBaseColorPass(LegacyRendererProfile profile);
        ~LegacyBaseColorPass();

        FrameGraphResource addPass(FrameGraphBuildContext& ctx, FrameGraphResource target = {});

    private:
        LegacyRendererProfile m_Profile;
        AndroidBaseColorPass* m_AndroidBaseColorPass {nullptr};
        WebGPUBaseColorPass*  m_WebGPUBaseColorPass {nullptr};
    };
} // namespace vultra

