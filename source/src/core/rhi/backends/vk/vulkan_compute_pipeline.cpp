#include "vultra/core/rhi/backends/vk/vulkan_compute_pipeline.hpp"

namespace vultra
{
    namespace rhi
    {
        VulkanComputePipeline::VulkanComputePipeline(const std::uintptr_t handle,
                                                                   const glm::uvec3     localSize) :
            m_Handle(handle), m_LocalSize(localSize)
        {}

        bool VulkanComputePipeline::isValid() const { return m_Handle != 0; }

        std::uintptr_t VulkanComputePipeline::getHandle() const { return m_Handle; }

        glm::uvec3 VulkanComputePipeline::getWorkGroupSize() const { return m_LocalSize; }
    } // namespace rhi
} // namespace vultra
