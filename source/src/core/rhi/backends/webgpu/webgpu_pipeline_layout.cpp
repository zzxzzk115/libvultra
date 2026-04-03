#include "vultra/core/rhi/backends/webgpu/webgpu_pipeline_layout.hpp"

#include <cassert>

namespace vultra
{
    namespace rhi
    {
        WebGPUPipelineLayout::WebGPUPipelineLayout(const std::uintptr_t                  handle,
                                                   std::vector<DescriptorSetLayoutKey>&& descriptorSetLayouts) :
            m_Handle(handle), m_DescriptorSetLayouts(std::move(descriptorSetLayouts))
        {}

        DescriptorSetLayoutKey WebGPUPipelineLayout::getDescriptorSet(const DescriptorSetIndex index) const
        {
            assert(index < m_DescriptorSetLayouts.size());
            return m_DescriptorSetLayouts[index];
        }
    } // namespace rhi
} // namespace vultra
