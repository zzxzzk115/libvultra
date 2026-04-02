#include "vultra/core/rhi/backends/vk/vulkan_pipeline_backend.hpp"

namespace vultra
{
    namespace rhi
    {
        VulkanPipelineBackend::VulkanPipelineBackend(const std::uintptr_t deviceHandle) :
            m_Device(vk::Device {reinterpret_cast<VkDevice>(deviceHandle)})
        {}

        void VulkanPipelineBackend::destroy(const std::uintptr_t pipelineHandle) noexcept
        {
            if (!m_Device || pipelineHandle == 0)
            {
                return;
            }
            m_Device.destroyPipeline(vk::Pipeline {reinterpret_cast<VkPipeline>(pipelineHandle)});
        }
    } // namespace rhi
} // namespace vultra
