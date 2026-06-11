#include "vultra/core/rhi/backends/vk/vulkan_render_device_access.hpp"

#include "vultra/core/rhi/backends/vk/handle_utils.hpp"
#include "vultra/core/rhi/backends/vk/vulkan_render_device.hpp"
#include "vultra/core/rhi/interfaces/render_device_access.hpp"
#include "vultra/core/rhi/render_device.hpp"

namespace vultra
{
    namespace rhi
    {
        std::uintptr_t VulkanRenderDeviceAccess::getInstanceHandle(const RenderDevice& rd)
        {
            if (const auto* backend = dynamic_cast<const VulkanRenderDevice*>(RenderDeviceAccess::get(rd)); backend)
            {
                return toBackendHandle(static_cast<VkInstance>(backend->m_Instance));
            }
            return 0;
        }

        std::uintptr_t VulkanRenderDeviceAccess::getPhysicalDeviceHandle(const RenderDevice& rd)
        {
            if (const auto* backend = dynamic_cast<const VulkanRenderDevice*>(RenderDeviceAccess::get(rd)); backend)
            {
                return toBackendHandle(static_cast<VkPhysicalDevice>(backend->m_PhysicalDevice));
            }
            return 0;
        }

        std::uintptr_t VulkanRenderDeviceAccess::getDeviceHandle(const RenderDevice& rd)
        {
            if (const auto* backend = dynamic_cast<const VulkanRenderDevice*>(RenderDeviceAccess::get(rd)); backend)
            {
                return toBackendHandle(static_cast<VkDevice>(backend->m_Device));
            }
            return 0;
        }

        int VulkanRenderDeviceAccess::getQueueFamilyIndex(const RenderDevice& rd)
        {
            if (const auto* backend = dynamic_cast<const VulkanRenderDevice*>(RenderDeviceAccess::get(rd)); backend)
            {
                return backend->m_GenericQueueFamilyIndex;
            }
            return -1;
        }

        std::uintptr_t VulkanRenderDeviceAccess::getQueueHandle(const RenderDevice& rd)
        {
            if (const auto* backend = dynamic_cast<const VulkanRenderDevice*>(RenderDeviceAccess::get(rd)); backend)
            {
                return toBackendHandle(static_cast<VkQueue>(backend->m_GenericQueue));
            }
            return 0;
        }

        std::uintptr_t VulkanRenderDeviceAccess::getPipelineCacheHandle(const RenderDevice& rd)
        {
            if (const auto* backend = dynamic_cast<const VulkanRenderDevice*>(RenderDeviceAccess::get(rd)); backend)
            {
                return toBackendHandle(static_cast<VkPipelineCache>(backend->m_PipelineCache));
            }
            return 0;
        }

        std::uintptr_t VulkanRenderDeviceAccess::getDescriptorPoolHandle(const RenderDevice& rd)
        {
            if (const auto* backend = dynamic_cast<const VulkanRenderDevice*>(RenderDeviceAccess::get(rd)); backend)
            {
                return toBackendHandle(static_cast<VkDescriptorPool>(backend->m_DefaultDescriptorPool));
            }
            return 0;
        }

        std::uintptr_t VulkanRenderDeviceAccess::getDescriptorSetLayoutHandle(const RenderDevice&          rd,
                                                                              const DescriptorSetLayoutKey layoutKey)
        {
            if (!dynamic_cast<const VulkanRenderDevice*>(RenderDeviceAccess::get(rd)))
            {
                return 0;
            }
            return RenderDeviceAccess::getDescriptorSetLayoutHandle(rd, layoutKey);
        }

        VulkanHookTable VulkanRenderDeviceAccess::getVulkanHooks(const RenderDevice& rd)
        {
            if (const auto* backend = dynamic_cast<const VulkanRenderDevice*>(RenderDeviceAccess::get(rd)); backend)
            {
                return backend->m_VulkanHooks;
            }
            return {};
        }
    } // namespace rhi
} // namespace vultra
