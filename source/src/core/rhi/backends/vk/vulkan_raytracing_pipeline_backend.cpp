#include "vultra/core/rhi/backends/vk/vulkan_raytracing_pipeline_backend.hpp"

namespace vultra
{
    namespace rhi
    {
        VulkanRayTracingPipelineBackend::VulkanRayTracingPipelineBackend(const std::uintptr_t handle,
                                                                         const RayTracingPipelineProperties properties) :
            m_Handle(handle), m_Properties(properties)
        {}

        bool VulkanRayTracingPipelineBackend::isValid() const { return m_Handle != 0; }

        std::uintptr_t VulkanRayTracingPipelineBackend::getHandle() const { return m_Handle; }

        RayTracingPipelineProperties VulkanRayTracingPipelineBackend::getProperties() const
        {
            return m_Properties;
        }
    } // namespace rhi
} // namespace vultra
