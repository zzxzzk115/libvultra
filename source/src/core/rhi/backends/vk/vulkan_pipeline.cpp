#include "vultra/core/rhi/backends/vk/vulkan_pipeline.hpp"
#include "vultra/core/rhi/backends/vk/handle_utils.hpp"

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
            m_Device.destroyPipeline(vk::Pipeline {asVkHandle<VkPipeline>(pipelineHandle)});
        }
    } // namespace rhi
} // namespace vultra
