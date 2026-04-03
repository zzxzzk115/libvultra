#pragma once

#include "vultra/core/rhi/interfaces/iswapchain.hpp"
#include "vultra/core/rhi/structs/swapchain_format.hpp"
#include "vultra/core/rhi/structs/vertical_sync.hpp"
#include "vultra/core/rhi/texture.hpp"

#include <vulkan/vulkan.hpp>

#include <optional>
#include <vector>

namespace vultra
{
    namespace os
    {
        class Window;
    }

    namespace rhi
    {
        class VulkanSwapchain final : public ISwapchain
        {
        public:
            VulkanSwapchain(std::uintptr_t instance,
                                   std::uintptr_t physicalDevice,
                                   std::uintptr_t device,
                                   os::Window*    window,
                                   SwapchainFormat format,
                                   VerticalSync   vsync);
            ~VulkanSwapchain() override;

            [[nodiscard]] bool            isValid() const override;
            [[nodiscard]] SwapchainFormat getFormat() const override;
            [[nodiscard]] PixelFormat     getPixelFormat() const override;
            [[nodiscard]] Extent2D        getExtent() const override;
            [[nodiscard]] std::size_t     getNumBuffers() const override;
            [[nodiscard]] std::uintptr_t  getHandle() const override;
            [[nodiscard]] const std::vector<Texture>& getBuffers() const override;
            [[nodiscard]] const Texture&              getBuffer(uint32_t index) const override;
            [[nodiscard]] uint32_t                    getCurrentBufferIndex() const override;
            [[nodiscard]] Texture&                    getCurrentBuffer() override;

            void recreate(std::optional<VerticalSync> vsync) override;
            [[nodiscard]] bool acquireNextImage(std::uintptr_t imageAcquired) override;

        private:
            void createSurface();
            void createSwapchain(SwapchainFormat format, VerticalSync vsync);
            void buildBuffers(Extent2D extent, PixelFormat pixelFormat);
            void destroy();

        private:
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
