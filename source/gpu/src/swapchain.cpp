#include <vultra/core/base/logger.hpp>
#include <vultra/gpu/swapchain.hpp>

namespace vultra
{
    namespace
    {
        std::expected<VriFormat, Error> supportedFormat(Device& device, VriFormat format)
        {
            if (format != VriFormat_BGRA8_SRGB && format != VriFormat_BGRA8_UNORM && format != VriFormat_RGBA8_SRGB &&
                format != VriFormat_RGBA8_UNORM)
            {
                return makeError("Swapchain requires an RGBA8 or BGRA8 UNORM/sRGB format");
            }
            if (device.core.GetFormatSupport(device.handle, format) & VriFormatSupport_ColorAttachment)
            {
                return format;
            }
            VriFormat alternate = VriFormat_Unknown;
            switch (format)
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
                return makeError("No RGBA/BGRA swapchain target supports the requested color encoding");
            }
            Logger::core().info("Swapchain format {} is unsupported; using {} with the same color encoding",
                                uint32_t(format),
                                uint32_t(alternate));
            return alternate;
        }
    } // namespace

    Swapchain::Swapchain(Device&                 device,
                         VriWindowHandle         window,
                         Extent                  size,
                         VriFormat               format,
                         std::function<Extent()> windowSize) :
        m_Device(device),
        m_Window(window),
        m_NativeSize(size),
        m_WindowSize(std::move(windowSize)),
        m_Format(format)
    {
    }

    std::expected<std::unique_ptr<Swapchain>, Error>
    Swapchain::create(Device& device, VriWindowHandle window, Extent size, VriFormat format)
    {
        return create(device, window, size, format, {});
    }

    std::expected<std::unique_ptr<Swapchain>, Error> Swapchain::create(Device&                 device,
                                                                       VriWindowHandle         window,
                                                                       Extent                  size,
                                                                       VriFormat               format,
                                                                       std::function<Extent()> windowSize)
    {
        const auto supported = supportedFormat(device, format);
        if (!supported)
        {
            return std::unexpected(supported.error());
        }
        std::unique_ptr<Swapchain> swapchain(new Swapchain(device, window, size, *supported, std::move(windowSize)));
        if (auto created = swapchain->createHandle(); !created)
        {
            return std::unexpected(created.error());
        }
        return swapchain;
    }

    std::expected<void, Error> Swapchain::resize(Extent size)
    {
        m_NativeSize = size;
        if (!m_WindowSize && !size.empty())
        {
            if (auto prepared = prepare(size); !prepared)
            {
                return std::unexpected(prepared.error());
            }
        }
        return {};
    }

    std::expected<void, Error> Swapchain::createHandle()
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
        if (auto created = check(m_Device.swap.CreateSwapChain(m_Device.handle, &desc, &m_Handle), "Create swapchain");
            !created)
        {
            m_Handle = nullptr;
            return created;
        }
        if (auto refreshed = refresh(); !refreshed)
        {
            destroyHandle();
            return refreshed;
        }
        return {};
    }

    void Swapchain::destroyHandle()
    {
        // Views must be destroyed before the swapchain destroys its images.
        m_Images.clear();
        if (m_Handle)
        {
            m_Device.swap.DestroySwapChain(m_Handle);
            m_Handle = nullptr;
        }
    }

    Swapchain::~Swapchain()
    {
        m_Device.waitIdleForDestruction();
        destroyHandle();
    }

    std::expected<void, Error> Swapchain::setVsync(bool enabled)
    {
        const auto mode = enabled ? VriPresentMode_Fifo : VriPresentMode_Immediate;
        if (m_PresentMode == mode || (m_WindowSize ? m_WindowSize() : m_NativeSize).empty())
        {
            return {};
        }
        if (auto idle = m_Device.waitIdle(); !idle)
        {
            return idle;
        }
        const auto previous = m_PresentMode;
        destroyHandle();
        m_PresentMode = mode;
        auto created  = createHandle();
        if (!created)
        {
            // Keep a usable swapchain in the previous mode and still report why the switch failed.
            m_PresentMode = previous;
            if (auto restored = createHandle(); !restored)
            {
                return restored;
            }
            return created;
        }
        m_Rebuild = false;
        return {};
    }

    std::expected<void, Error> Swapchain::refresh()
    {
        // Window resize notifications can lag behind the surface's actual image extent.
        if (auto queried = check(m_Device.swap.GetSwapChainExtent(m_Handle, &m_Extent.width, &m_Extent.height),
                                 "Get swapchain extent");
            !queried)
        {
            return queried;
        }
        uint32_t count = 0;
        if (auto counted =
                check(m_Device.swap.GetSwapChainTextures(m_Handle, nullptr, &count), "Count swapchain images");
            !counted)
        {
            return counted;
        }
        std::vector<VriTexture*> images(count);
        if (auto listed =
                check(m_Device.swap.GetSwapChainTextures(m_Handle, images.data(), &count), "Get swapchain images");
            !listed)
        {
            return listed;
        }
        images.resize(count);
        m_Images.clear();
        for (auto* image : images)
        {
            auto texture = Texture::create(m_Device, colorTexture(m_Extent, format()), image);
            if (!texture)
            {
                return std::unexpected(texture.error());
            }
            m_Images.push_back(std::move(*texture));
        }
        return {};
    }

    std::expected<bool, Error> Swapchain::prepare(Extent size)
    {
        if (m_Rebuild || size != m_Requested)
        {
            if (auto idle = m_Device.waitIdle(); !idle)
            {
                return std::unexpected(idle.error());
            }
            m_Images.clear(); // views must be destroyed before Resize destroys the images
            const auto result = m_Device.swap.Resize(m_Handle, size.width, size.height);
            m_Rebuild         = result == VriResult_OutOfDate;
            if (m_Rebuild)
            {
                return false;
            }
            if (auto resized = check(result, "Resize swapchain"); !resized)
            {
                return std::unexpected(resized.error());
            }
            m_Requested = size;
            if (auto refreshed = refresh(); !refreshed)
            {
                return std::unexpected(refreshed.error());
            }
        }
        return true;
    }

    std::expected<Texture*, Error> Swapchain::acquire()
    {
        const auto size = (m_WindowSize ? m_WindowSize() : m_NativeSize);
        if (size.empty())
        {
            return nullptr;
        }
        const auto prepared = prepare(size);
        if (!prepared)
        {
            return std::unexpected(prepared.error());
        }
        if (!*prepared)
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
        if (auto acquired = check(result, "Acquire swapchain image"); !acquired)
        {
            return std::unexpected(acquired.error());
        }
        if (index >= m_Images.size())
        {
            return makeError("Acquire swapchain image: index {} is outside the {} images", index, m_Images.size());
        }
        if (m_Device.core.GetDeviceDesc(m_Device.handle)->graphicsAPI == VriGraphicsAPI_WebGPU)
        {
            // VRI reuses a texture handle for successive browser canvas images. Its views do not persist.
            auto texture = Texture::create(m_Device, colorTexture(m_Extent, format()), m_Images[index].handle);
            if (!texture)
            {
                return std::unexpected(texture.error());
            }
            m_Images[index] = std::move(*texture);
        }
        return &m_Images[index];
    }

    std::expected<void, Error> Swapchain::present()
    {
        const auto result = m_Device.swap.Present(m_Handle, nullptr, 0);
        if (result == VriResult_OutOfDate)
        {
            m_Rebuild = true;
            return {};
        }
        return check(result, "Present");
    }
} // namespace vultra
