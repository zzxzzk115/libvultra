#pragma once

#include "vultra/core/rhi/backends/webgpu/webgpu_render_device.hpp"
#include "vultra/core/rhi/structs/pipeline_layout_structs.hpp"
#include "vultra/core/rhi/structs/resource_binding.hpp"
#include "vultra/core/rhi/structs/resource_indices.hpp"

#include <unordered_map>

namespace vultra
{
    namespace rhi
    {
        class WebGPUDescriptorSet final
        {
        public:
            WebGPUDescriptorSet(DescriptorSetLayoutKey layoutKey, std::unordered_map<BindingIndex, ResourceBinding> bindings);
            ~WebGPUDescriptorSet();

            WebGPUDescriptorSet(const WebGPUDescriptorSet&) = delete;
            WebGPUDescriptorSet(WebGPUDescriptorSet&&) noexcept = delete;
            WebGPUDescriptorSet& operator=(const WebGPUDescriptorSet&) = delete;
            WebGPUDescriptorSet& operator=(WebGPUDescriptorSet&&) noexcept = delete;

            [[nodiscard]] DescriptorSetLayoutKey layoutKey() const { return m_LayoutKey; }

            [[nodiscard]] WGPUBindGroup getOrCreateBindGroup(const WebGPURenderDevice& backend,
                                                             DescriptorSetLayoutKey   expectedLayoutKey);

        private:
            DescriptorSetLayoutKey                              m_LayoutKey {};
            std::unordered_map<BindingIndex, ResourceBinding>   m_Bindings;
            std::unordered_map<std::size_t, WGPUBindGroup>      m_BindGroups;
        };
    } // namespace rhi
} // namespace vultra
