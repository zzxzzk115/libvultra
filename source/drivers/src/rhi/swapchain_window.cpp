#include <vultra/drivers/rhi/swapchain.hpp>
#include <vultra/platform/window.hpp>

namespace vultra
{
#if defined(__APPLE__) && defined(VULTRA_WINDOW_GLFW)
    namespace platform
    {
        void* glfwMetalLayer(void* nativeWindow);
    }
#endif

    namespace
    {
        VriWindowHandle swapchainWindow(Device& device, Window& window)
        {
            auto handle = platform::nativeWindow(window);
#if defined(__APPLE__) && defined(VULTRA_WINDOW_GLFW)
            const auto api = device.core.GetDeviceDesc(device.handle)->graphicsAPI;
            if (api == VriGraphicsAPI_Vulkan || api == VriGraphicsAPI_Metal)
            {
                handle.handle.cocoa.layer = platform::glfwMetalLayer(handle.handle.cocoa.window);
            }
#else
            (void)device;
#endif
            return handle;
        }
    } // namespace

    std::expected<std::unique_ptr<Swapchain>, std::string>
    Swapchain::create(Device& device, Window& window, VriFormat format)
    {
        auto size = window.framebufferSizeExpected();
        if (!size)
            return std::unexpected(size.error());
        auto swapchain = create(device, swapchainWindow(device, window), *size, format);
        if (!swapchain)
            return std::unexpected(swapchain.error());
        (*swapchain)->m_WindowSize = [&window]
        {
            return window.framebufferSizeExpected();
        };
        return swapchain;
    }

    Swapchain::Swapchain(Device& device, Window& window, VriFormat format) :
        Swapchain(device, swapchainWindow(device, window), window.framebufferSize(), format)
    {
        m_WindowSize = [&window]
        {
            return window.framebufferSizeExpected();
        };
    }
} // namespace vultra
