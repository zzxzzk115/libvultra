#include "vultra/core/rhi/backends/vk/vulkan_raytracing_pipeline.hpp"

namespace vultra
{
    namespace rhi
    {
        VulkanRayTracingPipeline::VulkanRayTracingPipeline(const std::uintptr_t               handle,
                                                           const RayTracingPipelineProperties properties) :
            m_Handle(handle), m_Properties(properties)
        {}

        bool VulkanRayTracingPipeline::isValid() const { return m_Handle != 0; }

        std::uintptr_t VulkanRayTracingPipeline::getHandle() const { return m_Handle; }

        RayTracingPipelineProperties VulkanRayTracingPipeline::getProperties() const { return m_Properties; }
    } // namespace rhi
} // namespace vultra
