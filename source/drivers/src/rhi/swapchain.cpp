#include <vultra/drivers/rhi/swapchain.hpp>
#include <vultra/platform/window.hpp>

namespace vultra
{
    Swapchain::Swapchain(Device& device, Window& window, VriFormat format) :
        m_Device(device),
        m_Window(window),
        m_Format(format)
    {
        if (format != VriFormat_BGRA8_SRGB && format != VriFormat_BGRA8_UNORM)
        {
            throw std::invalid_argument("Desktop swapchain format must be BGRA8_SRGB or BGRA8_UNORM");
        }
        m_Requested = window.framebufferSize();
        VriSwapChainDesc desc {};
        desc.window      = platform::nativeWindow(window);
        desc.queue       = device.queue;
        desc.format      = m_Format;
        desc.width       = m_Requested.width;
        desc.height      = m_Requested.height;
        desc.textureNum  = 2;
        desc.presentMode = VriPresentMode_Fifo;
        check(device.swap.CreateSwapChain(device.handle, &desc, &m_Handle), "Create swapchain");
        try
        {
            refresh();
        }
        catch (...)
        {
            device.swap.DestroySwapChain(m_Handle);
            throw;
        }
    }

    Swapchain::~Swapchain()
    {
        m_Device.waitIdle();
        m_Images.clear();
        m_Device.swap.DestroySwapChain(m_Handle);
    }

    void Swapchain::refresh()
    {
        // Window resize notifications can lag behind the surface's actual image extent.
        check(m_Device.swap.GetSwapChainExtent(m_Handle, &m_Extent.width, &m_Extent.height), "Get swapchain extent");
        uint32_t count = 0;
        check(m_Device.swap.GetSwapChainTextures(m_Handle, nullptr, &count), "Count swapchain images");
        std::vector<VriTexture*> images(count);
        check(m_Device.swap.GetSwapChainTextures(m_Handle, images.data(), &count), "Get swapchain images");
        images.resize(count);
        m_Images.clear();
        for (auto* image : images)
        {
            m_Images.push_back(std::make_unique<Texture>(m_Device, colorTexture(m_Extent, format()), image));
        }
    }

    Texture* Swapchain::acquire()
    {
        const auto size = m_Window.framebufferSize();
        if (size.empty())
        {
            return nullptr;
        }
        if (m_Rebuild || size != m_Requested)
        {
            m_Device.waitIdle();
            m_Images.clear(); // views must be destroyed before Resize destroys the images
            const auto result = m_Device.swap.Resize(m_Handle, size.width, size.height);
            m_Rebuild         = result == VriResult_OutOfDate;
            if (m_Rebuild)
            {
                return nullptr;
            }
            check(result, "Resize swapchain");
            m_Requested = size;
            refresh();
        }
        uint32_t   index  = 0;
        const auto result = m_Device.swap.AcquireNextTexture(m_Handle, nullptr, 0, &index);
        if (result == VriResult_OutOfDate)
        {
            m_Rebuild = true;
            return nullptr;
        }
        check(result, "Acquire swapchain image");
        return m_Images.at(index).get();
    }

    void Swapchain::present()
    {
        const auto result = m_Device.swap.Present(m_Handle, nullptr, 0);
        if (result == VriResult_OutOfDate)
        {
            m_Rebuild = true;
        }
        else
        {
            check(result, "Present");
        }
    }
} // namespace vultra
