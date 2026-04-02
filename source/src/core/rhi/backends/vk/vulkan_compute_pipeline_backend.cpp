#include "vultra/core/rhi/backends/vk/vulkan_compute_pipeline_backend.hpp"

namespace vultra
{
    namespace rhi
    {
        VulkanComputePipelineBackend::VulkanComputePipelineBackend(const std::uintptr_t handle,
                                                                   const glm::uvec3     localSize) :
            m_Handle(handle), m_LocalSize(localSize)
        {}

        bool VulkanComputePipelineBackend::isValid() const { return m_Handle != 0; }

        std::uintptr_t VulkanComputePipelineBackend::getHandle() const { return m_Handle; }

        glm::uvec3 VulkanComputePipelineBackend::getWorkGroupSize() const { return m_LocalSize; }
    } // namespace rhi
} // namespace vultra
