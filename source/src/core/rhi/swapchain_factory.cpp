#include "vultra/core/rhi/interfaces/iswapchain.hpp"

#if !defined(__EMSCRIPTEN__)
#include "vultra/core/rhi/backends/vk/vulkan_swapchain.hpp"
#endif
#include "vultra/core/rhi/backends/webgpu/webgpu_swapchain.hpp"

#include <cassert>

namespace vultra
{
    namespace rhi
    {
        std::shared_ptr<ISwapchain> createSwapchain(const std::uintptr_t   instance,
                                                    const std::uintptr_t   physicalDevice,
                                                    const std::uintptr_t   device,
                                                    const RenderBackendApi backendApi,
                                                    os::Window*            window,
                                                    const SwapchainFormat  format,
                                                    const VerticalSync     vsync)
        {
            switch (backendApi)
            {
#if !defined(__EMSCRIPTEN__)
                case RenderBackendApi::eVulkan:
                case RenderBackendApi::eAuto:
                    return std::make_shared<VulkanSwapchain>(instance, physicalDevice, device, window, format, vsync);
#else
                case RenderBackendApi::eVulkan:
                case RenderBackendApi::eAuto:
                    return createWebGPUSwapchain(instance, physicalDevice, device, window, format, vsync);
#endif
                case RenderBackendApi::eWebGPU:
                    return createWebGPUSwapchain(instance, physicalDevice, device, window, format, vsync);
            }
            assert(false);
            return {};
        }
    } // namespace rhi
} // namespace vultra
