#pragma once

#include "vultra/core/rhi/interfaces/iswapchain.hpp"
#include "vultra/core/rhi/backends/webgpu/webgpu_render_device.hpp"

namespace vultra
{
    namespace os
    {
        class Window;
    }

    namespace rhi
    {
        [[nodiscard]] WGPUTexture getCurrentWebGPUSwapchainTexture();

        [[nodiscard]] std::shared_ptr<ISwapchain>
        createWebGPUSwapchain(std::uintptr_t instance,
                                     std::uintptr_t physicalDevice,
                                     std::uintptr_t device,
                                     os::Window*    window,
                                     SwapchainFormat format,
                                     VerticalSync   vsync);
    } // namespace rhi
} // namespace vultra
