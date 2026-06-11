#pragma once

#include "vultra/core/rhi/pipeline_layout.hpp"
#include "vultra/core/rhi/structs/vulkan_hook_table.hpp"

#include <cstdint>

namespace vultra
{
    namespace rhi
    {
        class RenderDevice;

        class VulkanRenderDeviceAccess final
        {
        public:
            [[nodiscard]] static std::uintptr_t getInstanceHandle(const RenderDevice&);
            [[nodiscard]] static std::uintptr_t getPhysicalDeviceHandle(const RenderDevice&);
            [[nodiscard]] static std::uintptr_t getDeviceHandle(const RenderDevice&);
            [[nodiscard]] static int            getQueueFamilyIndex(const RenderDevice&);
            [[nodiscard]] static std::uintptr_t getQueueHandle(const RenderDevice&);
            [[nodiscard]] static std::uintptr_t getPipelineCacheHandle(const RenderDevice&);
            [[nodiscard]] static std::uintptr_t getDescriptorPoolHandle(const RenderDevice&);
            [[nodiscard]] static std::uintptr_t getDescriptorSetLayoutHandle(const RenderDevice&, DescriptorSetLayoutKey);
            [[nodiscard]] static VulkanHookTable getVulkanHooks(const RenderDevice&);
        };
    } // namespace rhi
} // namespace vultra
