#include <vultra/core/base/logger.hpp>
#include <vultra/drivers/rhi/swapchain.hpp>

namespace vultra
{
    Swapchain::Swapchain(Device& device, VriWindowHandle window, Extent size, VriFormat format) :
        m_Device(device),
        m_Window(window),
        m_NativeSize(size),
        m_Format(format)
    {
        if (format != VriFormat_BGRA8_SRGB && format != VriFormat_BGRA8_UNORM && format != VriFormat_RGBA8_SRGB &&
            format != VriFormat_RGBA8_UNORM)
        {
            throw std::invalid_argument("Swapchain requires an RGBA8 or BGRA8 UNORM/sRGB format");
        }
        if (!(device.core.GetFormatSupport(device.handle, m_Format) & VriFormatSupport_ColorAttachment))
        {
            VriFormat alternate = VriFormat_Unknown;
            switch (m_Format)
            {
                case VriFormat_BGRA8_SRGB:
                    alternate = VriFormat_RGBA8_SRGB;
                    break;
                case VriFormat_BGRA8_UNORM:
                    alternate = VriFormat_RGBA8_UNORM;
                    break;
                case VriFormat_RGBA8_SRGB:
                    alternate = VriFormat_BGRA8_SRGB;
                    break;
                case VriFormat_RGBA8_UNORM:
                    alternate = VriFormat_BGRA8_UNORM;
                    break;
                default:
                    break;
            }
            if (!(device.core.GetFormatSupport(device.handle, alternate) & VriFormatSupport_ColorAttachment))
            {
                throw std::runtime_error("No RGBA/BGRA swapchain target supports the requested color encoding");
            }
            Logger::core().info("Swapchain format {} is unsupported; using {} with the same color encoding",
                                uint32_t(m_Format),
                                uint32_t(alternate));
            m_Format = alternate;
        }
        create();
    }

    void Swapchain::resize(Extent size)
    {
        m_NativeSize = size;
        if (!m_WindowSize && !size.empty())
        {
            prepare(size);
        }
    }

    void Swapchain::create()
    {
        m_Requested = (m_WindowSize ? m_WindowSize() : m_NativeSize);
        VriSwapChainDesc desc {};
        desc.window      = m_Window;
        desc.queue       = m_Device.queue;
        desc.format      = m_Format;
        desc.width       = m_Requested.width;
        desc.height      = m_Requested.height;
        desc.textureNum  = 2;
        desc.presentMode = m_PresentMode;
        check(m_Device.swap.CreateSwapChain(m_Device.handle, &desc, &m_Handle), "Create swapchain");
        try
        {
            refresh();
        }
        catch (...)
        {
            m_Device.swap.DestroySwapChain(m_Handle);
            m_Handle = nullptr;
            throw;
        }
    }

    Swapchain::~Swapchain()
    {
        m_Device.waitIdleForDestruction();
        m_Images.clear();
        m_Device.swap.DestroySwapChain(m_Handle);
    }

    void Swapchain::setVsync(bool enabled)
    {
        const auto mode = enabled ? VriPresentMode_Fifo : VriPresentMode_Immediate;
        if (m_PresentMode == mode || (m_WindowSize ? m_WindowSize() : m_NativeSize).empty())
        {
            return;
        }
        m_Device.waitIdle();
        const auto previous = m_PresentMode;
        m_Images.clear();
        m_Device.swap.DestroySwapChain(m_Handle);
        m_Handle      = nullptr;
        m_PresentMode = mode;
        try
        {
            create();
            m_Rebuild = false;
        }
        catch (...)
        {
            m_PresentMode = previous;
            create();
            throw;
        }
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

    bool Swapchain::prepare(Extent size)
    {
        if (m_Rebuild || size != m_Requested)
        {
            m_Device.waitIdle();
            m_Images.clear(); // views must be destroyed before Resize destroys the images
            const auto result = m_Device.swap.Resize(m_Handle, size.width, size.height);
            m_Rebuild         = result == VriResult_OutOfDate;
            if (m_Rebuild)
            {
                return false;
            }
            check(result, "Resize swapchain");
            m_Requested = size;
            refresh();
        }
        return true;
    }

    Texture* Swapchain::acquire()
    {
        const auto size = (m_WindowSize ? m_WindowSize() : m_NativeSize);
        if (size.empty())
        {
            return nullptr;
        }
        if (!prepare(size))
        {
            return nullptr;
        }
        uint32_t   index  = 0;
        const auto result = m_Device.swap.AcquireNextTexture(m_Handle, nullptr, 0, &index);
        if (result == VriResult_OutOfDate)
        {
            m_Rebuild = true;
            return nullptr;
        }
        check(result, "Acquire swapchain image");
        if (m_Device.core.GetDeviceDesc(m_Device.handle)->graphicsAPI == VriGraphicsAPI_WebGPU)
        {
            // VRI reuses a texture handle for successive browser canvas images. Its views do not persist.
            auto* image     = m_Images.at(index)->handle;
            m_Images[index] = std::make_unique<Texture>(m_Device, colorTexture(m_Extent, format()), image);
        }
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
