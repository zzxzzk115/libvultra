#pragma once
#include <vultra/drivers/rhi/resources.hpp>

#include <memory>
#include <vector>

namespace vultra
{
    class Swapchain
    {
    public:
        // sRGB attachments accept linear shader/clear colors. UNORM accepts display-encoded output.
        Swapchain(Device& device, Window& window, VriFormat format = VriFormat_BGRA8_SRGB);
        ~Swapchain();
        Swapchain(const Swapchain&)            = delete;
        Swapchain& operator=(const Swapchain&) = delete;
        // nullptr means minimized or temporarily out of date. Retry on the next iteration.
        Texture* acquire();
        void     present();

        Extent size() const
        {
            return m_Extent;
        }

        VriFormat format() const
        {
            return m_Format;
        }

    private:
        void                                  refresh();
        Device&                               m_Device;
        Window&                               m_Window;
        VriSwapChain*                         m_Handle = nullptr;
        VriFormat                             m_Format;
        Extent                                m_Extent {};
        Extent                                m_Requested {};
        bool                                  m_Rebuild = false;
        std::vector<std::unique_ptr<Texture>> m_Images;
    };
} // namespace vultra
