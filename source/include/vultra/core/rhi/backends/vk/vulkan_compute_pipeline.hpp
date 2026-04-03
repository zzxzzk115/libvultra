#pragma once

#include "vultra/core/rhi/interfaces/icompute_pipeline.hpp"

namespace vultra
{
    namespace rhi
    {
        class VulkanComputePipeline final : public IComputePipeline
        {
        public:
            VulkanComputePipeline(std::uintptr_t handle, glm::uvec3 localSize);

            [[nodiscard]] bool           isValid() const override;
            [[nodiscard]] std::uintptr_t getHandle() const override;
            [[nodiscard]] glm::uvec3     getWorkGroupSize() const override;

        private:
            std::uintptr_t m_Handle {0};
            glm::uvec3     m_LocalSize {};
        };
    } // namespace rhi
} // namespace vultra
