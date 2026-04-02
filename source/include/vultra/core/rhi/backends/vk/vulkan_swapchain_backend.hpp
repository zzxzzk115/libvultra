#pragma once

#include "vultra/core/rhi/interfaces/iswapchain_backend.hpp"
#include "vultra/core/rhi/structs/swapchain_format.hpp"
#include "vultra/core/rhi/structs/vertical_sync.hpp"
#include "vultra/core/rhi/texture.hpp"

#include <vulkan/vulkan.hpp>

#include <vector>

namespace vultra
{
    namespace os
    {
        class Window;
    }

    namespace rhi
    {
        struct VulkanSwapchainBackend final : ISwapchainBackend
        {
            os::Window*          m_Window {nullptr};
            vk::Instance         m_Instance {nullptr};
            vk::PhysicalDevice   m_PhysicalDevice {nullptr};
            vk::Device           m_Device {nullptr};
            vk::SurfaceKHR       m_Surface {nullptr};
            vk::SwapchainKHR     m_Handle {nullptr};
            SwapchainFormat      m_Format {SwapchainFormat::eLinear};
            VerticalSync         m_VerticalSync {VerticalSync::eDisabled};
            std::vector<Texture> m_Buffers;
            uint32_t             m_CurrentImageIndex {0};
        };
    } // namespace rhi
} // namespace vultra
