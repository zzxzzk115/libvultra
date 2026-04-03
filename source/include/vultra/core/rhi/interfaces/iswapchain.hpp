#pragma once

#include "vultra/core/rhi/structs/handles.hpp"
#include "vultra/core/rhi/structs/render_backend_api.hpp"
#include "vultra/core/rhi/structs/swapchain_format.hpp"
#include "vultra/core/rhi/structs/vertical_sync.hpp"
#include "vultra/core/rhi/texture.hpp"

#include <memory>
#include <optional>
#include <vector>

namespace vultra
{
    namespace os
    {
        class Window;
    }
}

namespace vultra
{
    namespace rhi
    {
        class ISwapchain
        {
        public:
            virtual ~ISwapchain() = default;

            [[nodiscard]] virtual bool            isValid() const             = 0;
            [[nodiscard]] virtual SwapchainFormat getFormat() const           = 0;
            [[nodiscard]] virtual PixelFormat     getPixelFormat() const      = 0;
            [[nodiscard]] virtual Extent2D        getExtent() const           = 0;
            [[nodiscard]] virtual std::size_t     getNumBuffers() const       = 0;
            [[nodiscard]] virtual std::uintptr_t  getHandle() const           = 0;
            [[nodiscard]] virtual const std::vector<Texture>& getBuffers() const = 0;
            [[nodiscard]] virtual const Texture&              getBuffer(uint32_t index) const = 0;
            [[nodiscard]] virtual uint32_t                    getCurrentBufferIndex() const    = 0;
            [[nodiscard]] virtual Texture&                    getCurrentBuffer()                = 0;

            virtual void recreate(std::optional<VerticalSync> vsync) = 0;
            [[nodiscard]] virtual bool acquireNextImage(std::uintptr_t imageAcquired) = 0;
        };

        [[nodiscard]] std::shared_ptr<ISwapchain>
        createSwapchain(std::uintptr_t instance,
                               std::uintptr_t physicalDevice,
                               std::uintptr_t device,
                               RenderBackendApi backendApi,
                               os::Window*    window,
                               SwapchainFormat format,
                               VerticalSync   vsync);
    } // namespace rhi
} // namespace vultra
