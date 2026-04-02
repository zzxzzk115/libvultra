#pragma once

#include <cstdint>
#include <memory>

#include "vultra/core/rhi/structs/rect2d.hpp"
#include "vultra/core/rhi/structs/swapchain_format.hpp"
#include "vultra/core/rhi/structs/vertical_sync.hpp"
#include "vultra/core/rhi/texture.hpp"
#include "vultra/core/rhi/interfaces/iswapchain_backend.hpp"

namespace vultra
{
    namespace os
    {
        class Window;
    }

    namespace rhi
    {
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

            [[nodiscard]] SwapchainFormat getFormat() const;
            [[nodiscard]] PixelFormat getPixelFormat() const;
            [[nodiscard]] Extent2D    getExtent() const;

            [[nodiscard]] std::size_t getNumBuffers() const;
            [[nodiscard]] std::uintptr_t getNativeHandle() const;

            [[nodiscard]] const std::vector<Texture>& getBuffers() const;
            [[nodiscard]] const Texture&              getBuffer(uint32_t) const;

            [[nodiscard]] uint32_t getCurrentBufferIndex() const;
            [[nodiscard]] Texture& getCurrentBuffer();

            void recreate(std::optional<VerticalSync> = std::nullopt);

            bool acquireNextImage(std::uintptr_t imageAcquired = 0);

        private:
            Swapchain(std::uintptr_t, std::uintptr_t, std::uintptr_t, os::Window*, SwapchainFormat, VerticalSync);
            void createSurface();
            void create(SwapchainFormat, VerticalSync);
            void buildBuffers(Extent2D, PixelFormat);
            void destroy();

        private:
            std::shared_ptr<ISwapchainBackend> m_Backend;
        };

        [[nodiscard]] Rect2D getRenderArea(const Swapchain&);
    } // namespace rhi
} // namespace vultra
