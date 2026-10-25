#pragma once
#include <vultra/drivers/rhi/resources.hpp>

#include <vri/ext/vri_ext_swapchain.h>

#include <functional>
#include <memory>
#include <vector>

namespace vultra
{
    class Window;

    class Swapchain
    {
    public:
        // sRGB attachments accept linear shader/clear colors. UNORM accepts display-encoded output.
        Swapchain(Device& device, Window& window, VriFormat format = VriFormat_BGRA8_SRGB);
        // The host keeps the native surface alive through destruction and publishes size changes explicitly.
        Swapchain(Device& device, VriWindowHandle window, Extent size, VriFormat format);
        ~Swapchain();
        void resize(Extent size);
        Swapchain(const Swapchain&)            = delete;
        Swapchain& operator=(const Swapchain&) = delete;
        // nullptr means minimized or temporarily out of date. Retry on the next iteration.
        Texture* acquire();
        void     present();
        void     setVsync(bool enabled); // Between completed frames, before acquiring an image.

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
        bool                                  prepare(Extent size);
        void                                  refresh();
        void                                  create();
        Device&                               m_Device;
        VriWindowHandle                       m_Window;
        Extent                                m_NativeSize;
        std::function<Extent()>               m_WindowSize;
        VriSwapChain*                         m_Handle = nullptr;
        VriFormat                             m_Format;
        Extent                                m_Extent {};
        Extent                                m_Requested {};
        bool                                  m_Rebuild     = false;
        VriPresentMode                        m_PresentMode = VriPresentMode_Fifo;
        std::vector<std::unique_ptr<Texture>> m_Images;
    };
} // namespace vultra
