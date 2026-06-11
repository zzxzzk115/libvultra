#pragma once

#include <cstdint>

namespace vultra::rhi
{
    struct VulkanHookTable
    {
        std::uintptr_t vkGetInstanceProcAddr {0};
        std::uintptr_t vkGetDeviceProcAddr {0};
        std::uintptr_t vkCreateInstance {0};
        std::uintptr_t vkCreateDevice {0};
        std::uintptr_t vkCreateSwapchainKHR {0};
        std::uintptr_t vkDestroySwapchainKHR {0};
        std::uintptr_t vkGetSwapchainImagesKHR {0};
        std::uintptr_t vkAcquireNextImageKHR {0};
        std::uintptr_t vkQueuePresentKHR {0};
        std::uintptr_t vkDeviceWaitIdle {0};
        std::uintptr_t vkCreateWin32SurfaceKHR {0};
        std::uintptr_t vkDestroySurfaceKHR {0};
        std::uintptr_t vkBeginCommandBuffer {0};
        std::uintptr_t vkCmdBindPipeline {0};
        std::uintptr_t vkCmdBindDescriptorSets {0};
        std::uintptr_t vkCmdPipelineBarrier {0};
        std::uintptr_t vkCreateImage {0};
        std::uintptr_t vkPostBeginCommandBuffer {0};
        std::uintptr_t vkPostCmdBindPipeline {0};
        std::uintptr_t vkPostCmdBindDescriptorSets {0};

        [[nodiscard]] bool empty() const
        {
            return vkGetInstanceProcAddr == 0 && vkGetDeviceProcAddr == 0 && vkCreateInstance == 0 &&
                   vkCreateDevice == 0 && vkCreateSwapchainKHR == 0 && vkDestroySwapchainKHR == 0 &&
                   vkGetSwapchainImagesKHR == 0 && vkAcquireNextImageKHR == 0 && vkQueuePresentKHR == 0 &&
                   vkDeviceWaitIdle == 0 && vkCreateWin32SurfaceKHR == 0 && vkDestroySurfaceKHR == 0 &&
                   vkBeginCommandBuffer == 0 && vkCmdBindPipeline == 0 && vkCmdBindDescriptorSets == 0 &&
                   vkCmdPipelineBarrier == 0 && vkCreateImage == 0 && vkPostBeginCommandBuffer == 0 &&
                   vkPostCmdBindPipeline == 0 && vkPostCmdBindDescriptorSets == 0;
        }
    };
} // namespace vultra::rhi
