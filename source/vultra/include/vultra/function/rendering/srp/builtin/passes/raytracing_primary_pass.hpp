#pragma once

#include "vultra/core/rhi/raytracing_pass.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"

namespace vultra
{
    class RayTracingPrimaryPass final : public rhi::RayTracingPass<RayTracingPrimaryPass>
    {
        friend class rhi::BasePass<RayTracingPrimaryPass, rhi::RayTracingPipeline>;

    public:
        RayTracingPrimaryPass();

        FrameGraphResource addPass(FrameGraphBuildContext& ctx);

    private:
        rhi::RayTracingPipeline createPipeline();
    };
} // namespace vultra
