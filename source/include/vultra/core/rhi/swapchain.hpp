#pragma once

#include "vultra/core/rhi/rect2d.hpp"
#include "vultra/core/rhi/texture.hpp"

namespace vultra
{
    namespace os
    {
        class Window;
    }

    namespace rhi
    {
        enum class VerticalSync
        {
            eDisabled,
            eEnabled,
            eAdaptive
        };

        class Swapchain final
        {
            friend class RenderDevice;

        public:
            Swapchain()                 = default;
            Swapchain(const Swapchain&) = delete;
            Swapchain(Swapchain&&) noexcept;
            ~Swapchain();

            Swapchain& operator=(const Swapchain&) = delete;
            Swapchain& operator=(Swapchain&&) noexcept;

            [[nodiscard]] explicit operator bool() const;

            enum class Format
            {
                eLinear,
                esRGB
            };

            [[nodiscard]] Format      getFormat() const;
            [[nodiscard]] PixelFormat getPixelFormat() const;
            [[nodiscard]] Extent2D    getExtent() const;

            [[nodiscard]] std::size_t getNumBuffers() const;

            [[nodiscard]] const std::vector<Texture>& getBuffers() const;
            [[nodiscard]] const Texture&              getBuffer(uint32_t) const;

            [[nodiscard]] uint32_t getCurrentBufferIndex() const;
            [[nodiscard]] Texture& getCurrentBuffer();

            void recreate(std::optional<VerticalSync> = std::nullopt);

            bool acquireNextImage(vk::Semaphore imageAcquired = nullptr);

        private:
            Swapchain(vk::Instance, vk::PhysicalDevice, vk::Device, os::Window*, Format, VerticalSync);

            void createSurface();

            void create(Format, VerticalSync);
            void buildBuffers(Extent2D, PixelFormat);
            void destroy();

        private:
            os::Window* m_Window {nullptr};

            vk::Instance       m_Instance {nullptr};
            vk::PhysicalDevice m_PhysicalDevice {nullptr};
            vk::Device         m_Device {nullptr};

            vk::SurfaceKHR   m_Surface {nullptr};
            vk::SwapchainKHR m_Handle {nullptr};

            Format       m_Format {Format::eLinear};
            VerticalSync m_VerticalSync {VerticalSync::eEnabled};

            std::vector<Texture> m_Buffers;
            uint32_t             m_CurrentImageIndex {0};
        };

        [[nodiscard]] Rect2D getRenderArea(const Swapchain&);
    } // namespace rhi
} // namespace vultra