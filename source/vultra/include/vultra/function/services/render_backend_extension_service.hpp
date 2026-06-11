#pragma once

#include <vultra/core/rhi/structs/vulkan_hook_table.hpp>
#include <vbase/service/service_registry.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace vultra
{
    namespace rhi
    {
        class RenderDevice;
        class Swapchain;
    }

    using VulkanHookTable = rhi::VulkanHookTable;

    struct VulkanNativeDevice
    {
        std::uintptr_t instance {0};
        std::uintptr_t physicalDevice {0};
        std::uintptr_t device {0};
        std::uintptr_t graphicsQueue {0};
        uint32_t       graphicsQueueFamily {0};
        uint32_t       graphicsQueueIndex {0};

        [[nodiscard]] bool valid() const
        {
            return instance != 0 && physicalDevice != 0 && device != 0 && graphicsQueue != 0;
        }
    };

    struct VulkanDeviceRequirements
    {
        std::vector<std::string> deviceExtensions;
    };

    class IRenderBackendExtension
    {
    public:
        virtual ~IRenderBackendExtension() = default;

        [[nodiscard]] virtual std::string_view name() const = 0;
        [[nodiscard]] virtual VulkanHookTable  vulkanHooks() const { return {}; }
        virtual void collectVulkanDeviceRequirements(VulkanDeviceRequirements&) const {}

        virtual void beforeVulkanInstanceCreate() {}
        virtual void beforeVulkanDeviceCreate() {}
        virtual void afterVulkanDeviceCreate(rhi::RenderDevice&) {}
        virtual void afterVulkanNativeDeviceCreate(const VulkanNativeDevice&) {}
        virtual void beforeSwapchainCreate() {}
        virtual void afterSwapchainCreate(rhi::Swapchain&) {}
        virtual void beforeSwapchainDestroy() {}
        virtual void afterSwapchainDestroy() {}
        virtual void beforeAcquireNextImage() {}
        virtual void afterAcquireNextImage(bool) {}
        virtual void beforePresent() {}
        virtual void afterPresent() {}
        virtual void beforeDeviceWaitIdle() {}
        virtual void afterDeviceWaitIdle() {}
    };

    class IRenderBackendExtensionService
    {
    public:
        SERVICE_REGISTER(IRenderBackendExtensionService)

        virtual bool registerExtension(IRenderBackendExtension& extension) = 0;
        virtual void unregisterExtension(IRenderBackendExtension& extension) = 0;

        [[nodiscard]] virtual IRenderBackendExtension* extension() const = 0;
        [[nodiscard]] virtual VulkanHookTable          vulkanHooks() const = 0;
    };
} // namespace vultra
