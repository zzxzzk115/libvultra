#pragma once

#include <vulkan/vulkan.hpp>

namespace vultra
{
    namespace rhi
    {
        struct JobInfo
        {
            vk::Semaphore           wait {nullptr};
            vk::PipelineStageFlags2 waitStage {vk::PipelineStageFlagBits2::eAllCommands};
            vk::Semaphore           signal {nullptr};
        };
    } // namespace rhi
} // namespace vultra
