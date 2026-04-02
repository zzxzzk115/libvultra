#pragma once

#include "vultra/core/rhi/interfaces/iraytracing_pipeline_backend.hpp"

namespace vultra
{
    namespace rhi
    {
        class VulkanRayTracingPipelineBackend final : public IRayTracingPipelineBackend
        {
        public:
            VulkanRayTracingPipelineBackend(std::uintptr_t handle, RayTracingPipelineProperties properties);

            [[nodiscard]] bool                         isValid() const override;
            [[nodiscard]] std::uintptr_t              getHandle() const override;
            [[nodiscard]] RayTracingPipelineProperties getProperties() const override;

        private:
            std::uintptr_t              m_Handle {0};
            RayTracingPipelineProperties m_Properties {};
        };
    } // namespace rhi
} // namespace vultra
