#include "vultra/core/rhi/swapchain.hpp"

#include "vultra/core/rhi/interfaces/iswapchain.hpp"

namespace vultra
{
    namespace rhi
    {
        Swapchain::Swapchain(Swapchain&& other) noexcept : m_Backend(std::move(other.m_Backend)) {}

        Swapchain::~Swapchain() { destroy(); }

        Swapchain& Swapchain::operator=(Swapchain&& rhs) noexcept
        {
            if (this != &rhs)
            {
                destroy();
                std::swap(m_Backend, rhs.m_Backend);
            }
            return *this;
        }

        Swapchain::operator bool() const { return m_Backend && m_Backend->isValid(); }

        SwapchainFormat Swapchain::getFormat() const { return m_Backend->getFormat(); }

        PixelFormat Swapchain::getPixelFormat() const { return m_Backend->getPixelFormat(); }

        Extent2D Swapchain::getExtent() const { return m_Backend->getExtent(); }

        std::size_t Swapchain::getNumBuffers() const { return m_Backend->getNumBuffers(); }

        std::uintptr_t Swapchain::getHandle() const { return m_Backend->getHandle(); }

        const std::vector<Texture>& Swapchain::getBuffers() const { return m_Backend->getBuffers(); }

        const Texture& Swapchain::getBuffer(const uint32_t index) const { return m_Backend->getBuffer(index); }

        uint32_t Swapchain::getCurrentBufferIndex() const { return m_Backend->getCurrentBufferIndex(); }

        Texture& Swapchain::getCurrentBuffer() { return m_Backend->getCurrentBuffer(); }

        void Swapchain::recreate(const std::optional<VerticalSync> vsync) { m_Backend->recreate(vsync); }

        bool Swapchain::acquireNextImage(const std::uintptr_t imageAcquired)
        {
            return m_Backend->acquireNextImage(imageAcquired);
        }

        Swapchain::Swapchain(const std::uintptr_t   instance,
                             const std::uintptr_t   physicalDevice,
                             const std::uintptr_t   device,
                             const RenderBackendApi backendApi,
                             os::Window*            window,
                             const SwapchainFormat  format,
                             const VerticalSync     vsync)
        {
            m_Backend = createSwapchain(instance, physicalDevice, device, backendApi, window, format, vsync);
        }

        void Swapchain::destroy() { m_Backend.reset(); }

        Rect2D getRenderArea(const Swapchain& swapchain)
        {
            return Rect2D {.offset = {0, 0}, .extent = {swapchain.getExtent()}};
        }
    } // namespace rhi
} // namespace vultra
