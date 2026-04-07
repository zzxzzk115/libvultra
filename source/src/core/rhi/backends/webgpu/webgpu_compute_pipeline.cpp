#include "vultra/core/rhi/backends/webgpu/webgpu_compute_pipeline.hpp"

namespace vultra
{
    namespace rhi
    {
        WebGPUComputePipeline::WebGPUComputePipeline(const std::uintptr_t handle, const glm::uvec3 localSize) :
            m_Handle(handle), m_LocalSize(localSize)
        {}

        bool WebGPUComputePipeline::isValid() const { return m_Handle != 0; }

        std::uintptr_t WebGPUComputePipeline::getHandle() const { return m_Handle; }

        glm::uvec3 WebGPUComputePipeline::getWorkGroupSize() const { return m_LocalSize; }
    } // namespace rhi
} // namespace vultra
