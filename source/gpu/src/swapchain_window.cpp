#include <vultra/gpu/swapchain.hpp>
#include <vultra/platform/window.hpp>

namespace vultra
{
    std::expected<std::unique_ptr<Swapchain>, Error> Swapchain::create(Device& device, Window& window, VriFormat format)
    {
        return create(device,
                      platform::nativeWindow(window),
                      window.framebufferSize(),
                      format,
                      [&window]
                      {
                          return window.framebufferSize();
                      });
    }
} // namespace vultra
