#pragma once

#include "vultra/core/rhi/interfaces/ipipeline_backend.hpp"

#include <vulkan/vulkan.hpp>

namespace vultra
{
    namespace rhi
    {
        class VulkanPipelineBackend final : public IPipelineBackend
        {
        public:
            explicit VulkanPipelineBackend(std::uintptr_t deviceHandle);

            void destroy(std::uintptr_t pipelineHandle) noexcept override;

        private:
            vk::Device m_Device {nullptr};
        };
    } // namespace rhi
} // namespace vultra
