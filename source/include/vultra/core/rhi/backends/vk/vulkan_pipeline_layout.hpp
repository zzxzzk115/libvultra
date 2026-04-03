#pragma once

#include "vultra/core/rhi/interfaces/ipipeline_layout.hpp"

#include <vector>

namespace vultra
{
    namespace rhi
    {
        class VulkanPipelineLayout final : public IPipelineLayout
        {
        public:
            VulkanPipelineLayout(std::uintptr_t handle, std::vector<DescriptorSetLayoutKey>&& descriptorSetLayouts);
            ~VulkanPipelineLayout() override = default;

            [[nodiscard]] bool isValid() const override { return m_Handle != 0; }
            [[nodiscard]] std::uintptr_t getHandle() const override { return m_Handle; }
            [[nodiscard]] DescriptorSetLayoutKey getDescriptorSet(DescriptorSetIndex index) const override;

        private:
            std::uintptr_t m_Handle {0};
            std::vector<DescriptorSetLayoutKey> m_DescriptorSetLayouts;
        };
    } // namespace rhi
} // namespace vultra
