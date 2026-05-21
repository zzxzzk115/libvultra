#pragma once

#include "vultra/core/rhi/render_pass.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"
#include "vultra/function/rendering/render_structs.hpp"

#include <fg/Fwd.hpp>

namespace vultra
{
    struct ShadowPassResult
    {
        FrameGraphResource shadowMap;
        FrameGraphResource shadowData;
    };

    class ShadowMapPass final : public rhi::RenderPass<ShadowMapPass>
    {
        friend class BasePass;

    public:
        ShadowMapPass();

        ShadowPassResult addPass(FrameGraphBuildContext& ctx, const ShadowRenderSettings& settings);

    private:
        rhi::GraphicsPipeline createPipeline(uint32_t positionOffset, uint32_t vertexStride) const;
    };
} // namespace vultra
