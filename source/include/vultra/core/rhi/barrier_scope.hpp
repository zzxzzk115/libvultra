#pragma once

#include "vultra/core/rhi/access.hpp"
#include "vultra/core/rhi/pipeline_stage.hpp"

namespace vultra
{
    namespace rhi
    {
        struct BarrierScope
        {
            PipelineStages stageMask {PipelineStages::eNone};
            Access         accessMask {Access::eNone};

            bool operator==(const BarrierScope&) const = default;
        };

        constexpr auto kInitialBarrierScope = BarrierScope {
            .stageMask  = PipelineStages::eTop,
            .accessMask = Access::eNone,
        };
        constexpr auto kFatScope = BarrierScope {
            .stageMask  = PipelineStages::eAllCommands,
            .accessMask = Access::eMemoryRead | Access::eMemoryWrite,
        };
    } // namespace rhi
} // namespace vultra