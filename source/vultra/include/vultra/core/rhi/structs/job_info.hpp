#pragma once

#include "vultra/core/rhi/structs/handles.hpp"
#include "vultra/core/rhi/structs/pipeline_stage.hpp"

namespace vultra
{
    namespace rhi
    {
        struct JobInfo
        {
            SemaphoreHandle wait;
            PipelineStages  waitStage {PipelineStages::eAllCommands};
            SemaphoreHandle signal;
        };
    } // namespace rhi
} // namespace vultra
