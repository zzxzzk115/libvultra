#include "vultra/core/rhi/backends/vk/vulkan_pipeline_layout.hpp"

#include <cassert>

namespace vultra
{
    namespace rhi
    {
        VulkanPipelineLayout::VulkanPipelineLayout(
            const std::uintptr_t handle,
            std::vector<DescriptorSetLayoutKey>&& descriptorSetLayouts) :
            m_Handle(handle), m_DescriptorSetLayouts(std::move(descriptorSetLayouts))
        {}

        DescriptorSetLayoutKey VulkanPipelineLayout::getDescriptorSet(const DescriptorSetIndex index) const
        {
            assert(index < m_DescriptorSetLayouts.size());
            return m_DescriptorSetLayouts[index];
        }
    } // namespace rhi
} // namespace vultra
