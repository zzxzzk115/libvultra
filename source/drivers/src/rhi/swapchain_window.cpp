#include <vultra/drivers/rhi/swapchain.hpp>
#include <vultra/platform/window.hpp>

namespace vultra
{
    Swapchain::Swapchain(Device& device, Window& window, VriFormat format) :
        Swapchain(device, platform::nativeWindow(window), window.framebufferSize(), format)
    {
        m_WindowSize = [&window]
        {
            return window.framebufferSize();
        };
    }
} // namespace vultra
