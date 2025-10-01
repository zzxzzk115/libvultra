#pragma once

#include "vultra/core/rhi/base_pass.hpp"
#include "vultra/core/rhi/raytracing/raytracing_pipeline.hpp"

namespace vultra
{
    namespace rhi
    {
        template<class TargetPass>
        using RayTracingPass = BasePass<TargetPass, RayTracingPipeline>;
    }
} // namespace vultra