#pragma once

#include "vultra/core/rhi/base_pass.hpp"
#include "vultra/core/rhi/compute_pipeline.hpp"

namespace vultra
{
    namespace rhi
    {
        template<class TargetPass>
        using ComputePass = BasePass<TargetPass, ComputePipeline>;
    }
} // namespace vultra