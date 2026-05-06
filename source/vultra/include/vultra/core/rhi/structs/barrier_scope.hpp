#pragma once

#include "vultra/core/rhi/structs/access.hpp"
#include "vultra/core/rhi/structs/image_layout.hpp"
#include "vultra/core/rhi/structs/pipeline_stage.hpp"

namespace vultra
{
    namespace rhi
    {
        struct BarrierScope
        {
            PipelineStages srcStage {PipelineStages::eNone};
            Access         srcAccess {Access::eNone};
            PipelineStages dstStage {PipelineStages::eNone};
            Access         dstAccess {Access::eNone};
            ImageLayout    srcLayout {ImageLayout::eUndefined};
            ImageLayout    dstLayout {ImageLayout::eUndefined};
        };

        inline constexpr BarrierScope kInitialBarrierScope {};
    } // namespace rhi
} // namespace vultra
