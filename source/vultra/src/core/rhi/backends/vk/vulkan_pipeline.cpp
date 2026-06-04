#include "vultra/core/rhi/backends/vk/vulkan_pipeline.hpp"
#include "vultra/core/rhi/backends/vk/handle_utils.hpp"
#include "vultra/core/rhi/deferred_deletion_queue.hpp"

namespace vultra
{
    namespace rhi
    {
        VulkanPipeline::VulkanPipeline(const std::uintptr_t deviceHandle) :
            m_Device(vk::Device {reinterpret_cast<VkDevice>(deviceHandle)})
        {}

        void VulkanPipeline::destroy(const std::uintptr_t pipelineHandle) noexcept
        {
            if (!m_Device || pipelineHandle == 0)
            {
                return;
            }
            // Defer: gaussian-splat sorter / scene pipelines can be torn down mid-frame on a scene
            // switch while the current command buffer still has them bound.
            DeferredDeletionQueue::get().enqueue(
                [device = m_Device, pipeline = vk::Pipeline {asVkHandle<VkPipeline>(pipelineHandle)}]() {
                    device.destroyPipeline(pipeline);
                });
        }
    } // namespace rhi
} // namespace vultra
