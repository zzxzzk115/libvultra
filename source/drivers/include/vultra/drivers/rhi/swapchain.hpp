#pragma once
#include <vultra/drivers/rhi/resources.hpp>

#include <vri/ext/vri_ext_swapchain.h>

#include <expected>
#include <functional>
#include <memory>
#include <string>
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
        static std::expected<std::unique_ptr<Swapchain>, std::string>
        create(Device& device, Window& window, VriFormat format = VriFormat_BGRA8_SRGB);
        static std::expected<std::unique_ptr<Swapchain>, std::string>
        create(Device& device, VriWindowHandle window, Extent size, VriFormat format);
        ~Swapchain();
        void                             resize(Extent size);
        std::expected<void, std::string> resizeExpected(Extent size);
        Swapchain(const Swapchain&)            = delete;
        Swapchain& operator=(const Swapchain&) = delete;
        // nullptr means minimized or temporarily out of date. Retry on the next iteration.
        Texture* acquire();
        void     present();
        void     setVsync(bool enabled); // Between completed frames, before acquiring an image.
        // A null acquired texture means minimized or temporarily out of date; retry on a later frame.
        // If wrapping an acquired image fails, present or destroy the swapchain before retrying.
        std::expected<Texture*, std::string> acquireExpected();
        std::expected<void, std::string>     presentExpected();
        std::expected<void, std::string>     setVsyncExpected(bool enabled);

        bool vsync() const
        {
            return m_PresentMode == VriPresentMode_Fifo;
        }

        Extent size() const
        {
            return m_Extent;
        }

        // If initially minimized, the format is provisional until the first successful acquire.
        VriFormat format() const
        {
            return m_Format;
        }

    private:
        struct Uninitialized
        {};

        Swapchain(Device& device, VriWindowHandle window, Extent size, VriFormat format, Uninitialized);
        std::expected<void, std::string>                    initializeExpected();
        std::expected<void, std::string>                    createHandleExpected();
        std::expected<void, std::string>                    refreshExpected();
        std::expected<bool, std::string>                    prepareExpected(Extent size);
        std::expected<Extent, std::string>                  surfaceSizeExpected() const;
        Device&                                             m_Device;
        VriWindowHandle                                     m_Window;
        Extent                                              m_NativeSize;
        std::function<std::expected<Extent, std::string>()> m_WindowSize;
        VriSwapChain*                                       m_Handle = nullptr;
        VriFormat                                           m_Format;
        Extent                                              m_Extent {};
        Extent                                              m_Requested {};
        bool                                                m_FormatNegotiated = false;
        bool                                                m_Rebuild          = false;
        bool                                                m_Acquired         = false;
        VriPresentMode                                      m_PresentMode      = VriPresentMode_Fifo;
        std::vector<std::unique_ptr<Texture>>               m_Images;
    };
} // namespace vultra
