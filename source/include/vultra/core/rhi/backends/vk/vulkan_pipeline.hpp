#pragma once

#include "vultra/core/rhi/interfaces/ipipeline.hpp"

#include <vulkan/vulkan.hpp>

namespace vultra
{
    namespace rhi
    {
        class VulkanPipeline final : public IPipeline
        {
        public:
            explicit VulkanPipeline(std::uintptr_t deviceHandle);

            void destroy(std::uintptr_t pipelineHandle) noexcept override;

        private:
            vk::Device m_Device {nullptr};
        };
    } // namespace rhi
} // namespace vultra
