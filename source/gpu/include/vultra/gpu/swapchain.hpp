#pragma once
#include <vultra/gpu/resources.hpp>

#include <vri/ext/vri_ext_swapchain.h>

#include <expected>
#include <functional>
#include <memory>
#include <vector>

namespace vultra
{
    class Window;

    // Acquired images are borrowed from the swapchain, so it lives behind a unique_ptr and never moves.
    class Swapchain
    {
    public:
        // sRGB attachments accept linear shader/clear colors. UNORM accepts display-encoded output.
        [[nodiscard]] static std::expected<std::unique_ptr<Swapchain>, Error>
        create(Device& device, Window& window, VriFormat format = VriFormat_BGRA8_SRGB);
        // The host keeps the native surface alive through destruction and publishes size changes explicitly.
        [[nodiscard]] static std::expected<std::unique_ptr<Swapchain>, Error>
        create(Device& device, VriWindowHandle window, Extent size, VriFormat format);
        ~Swapchain();
        [[nodiscard]] std::expected<void, Error> resize(Extent size);
        Swapchain(const Swapchain&)            = delete;
        Swapchain& operator=(const Swapchain&) = delete;
        // nullptr means minimized or temporarily out of date. Retry on the next iteration.
        [[nodiscard]] std::expected<Texture*, Error> acquire();
        [[nodiscard]] std::expected<void, Error>     present();
        // Between completed frames, before acquiring an image.
        [[nodiscard]] std::expected<void, Error> setVsync(bool enabled);

        bool vsync() const
        {
            return m_PresentMode == VriPresentMode_Fifo;
        }

        Extent size() const
        {
            return m_Extent;
        }

        VriFormat format() const
        {
            return m_Format;
        }

    private:
        Swapchain(Device&                 device,
                  VriWindowHandle         window,
                  Extent                  size,
                  VriFormat               format,
                  std::function<Extent()> windowSize);
        static std::expected<std::unique_ptr<Swapchain>, Error> create(Device&                 device,
                                                                       VriWindowHandle         window,
                                                                       Extent                  size,
                                                                       VriFormat               format,
                                                                       std::function<Extent()> windowSize);
        std::expected<bool, Error>                              prepare(Extent size);
        std::expected<void, Error>                              refresh();
        std::expected<void, Error>                              createHandle();
        void                                                    destroyHandle();
        Device&                                                 m_Device;
        VriWindowHandle                                         m_Window;
        Extent                                                  m_NativeSize;
        std::function<Extent()>                                 m_WindowSize;
        VriSwapChain*                                           m_Handle = nullptr;
        VriFormat                                               m_Format;
        Extent                                                  m_Extent {};
        Extent                                                  m_Requested {};
        bool                                                    m_Rebuild     = false;
        VriPresentMode                                          m_PresentMode = VriPresentMode_Fifo;
        std::vector<Texture>                                    m_Images;
    };
} // namespace vultra
