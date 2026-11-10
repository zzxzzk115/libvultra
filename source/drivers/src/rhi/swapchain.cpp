#include <vultra/core/base/logger.hpp>
#include <vultra/drivers/rhi/swapchain.hpp>

#include <stdexcept>

namespace vultra
{
    namespace
    {
        VriFormat alternateFormat(VriFormat format)
        {
            switch (format)
            {
                case VriFormat_BGRA8_SRGB:
                    return VriFormat_RGBA8_SRGB;
                case VriFormat_BGRA8_UNORM:
                    return VriFormat_RGBA8_UNORM;
                case VriFormat_RGBA8_SRGB:
                    return VriFormat_BGRA8_SRGB;
                case VriFormat_RGBA8_UNORM:
                    return VriFormat_BGRA8_UNORM;
                default:
                    return VriFormat_Unknown;
            }
        }
    } // namespace

    Swapchain::Swapchain(Device& device, VriWindowHandle window, Extent size, VriFormat format, Uninitialized) :
        m_Device(device),
        m_Window(window),
        m_NativeSize(size),
        m_Format(format)
    {
    }

    std::expected<std::unique_ptr<Swapchain>, std::string>
    Swapchain::create(Device& device, VriWindowHandle window, Extent size, VriFormat format)
    {
        auto swapchain = std::unique_ptr<Swapchain>(new Swapchain(device, window, size, format, Uninitialized {}));
        if (auto initialized = swapchain->initializeExpected(); !initialized)
            return std::unexpected(initialized.error());
        return swapchain;
    }

    Swapchain::Swapchain(Device& device, VriWindowHandle window, Extent size, VriFormat format) :
        Swapchain(device, window, size, format, Uninitialized {})
    {
        if (auto initialized = initializeExpected(); !initialized)
            throw std::runtime_error(initialized.error());
    }

    std::expected<void, std::string> Swapchain::initializeExpected()
    {
        if (m_Format != VriFormat_BGRA8_SRGB && m_Format != VriFormat_BGRA8_UNORM && m_Format != VriFormat_RGBA8_SRGB &&
            m_Format != VriFormat_RGBA8_UNORM)
            return std::unexpected("Swapchain requires an RGBA8 or BGRA8 UNORM/sRGB format");
        if (!(m_Device.core.GetFormatSupport(m_Device.handle, m_Format) & VriFormatSupport_ColorAttachment))
        {
            const auto alternate = alternateFormat(m_Format);
            if (!(m_Device.core.GetFormatSupport(m_Device.handle, alternate) & VriFormatSupport_ColorAttachment))
                return std::unexpected("No RGBA/BGRA swapchain target supports the requested color encoding");
            Logger::core().info("Swapchain format {} is unsupported; using {} with the same color encoding",
                                uint32_t(m_Format),
                                uint32_t(alternate));
            m_Format = alternate;
        }
        if (m_NativeSize.empty())
            return {};
        return createHandleExpected();
    }

    std::expected<Extent, std::string> Swapchain::surfaceSizeExpected() const
    {
        return m_WindowSize ? m_WindowSize() : std::expected<Extent, std::string>(m_NativeSize);
    }

    std::expected<void, std::string> Swapchain::createHandleExpected()
    {
        auto currentSize = surfaceSizeExpected();
        if (!currentSize)
            return std::unexpected(currentSize.error());
        const auto size = *currentSize;
        if (size.empty())
            return std::unexpected("Cannot create a swapchain for an empty surface");
        VriSwapChainDesc desc {};
        desc.window          = m_Window;
        desc.queue           = m_Device.queue;
        desc.format          = m_Format;
        desc.width           = size.width;
        desc.height          = size.height;
        desc.textureNum      = 2;
        desc.presentMode     = m_PresentMode;
        const auto requested = m_Format;
        auto       status    = m_Device.swap.CreateSwapChain(m_Device.handle, &desc, &m_Handle);
        if (status == VriResult_Unsupported && !m_FormatNegotiated)
        {
            if (m_Handle)
            {
                m_Device.swap.DestroySwapChain(m_Handle);
                m_Handle = nullptr;
            }
            const auto alternate = alternateFormat(m_Format);
            if (m_Device.core.GetFormatSupport(m_Device.handle, alternate) & VriFormatSupport_ColorAttachment)
            {
                desc.format = alternate;
                status      = m_Device.swap.CreateSwapChain(m_Device.handle, &desc, &m_Handle);
                if (status == VriResult_Success && m_Handle)
                {
                    m_Format = alternate;
                    Logger::core().info("Swapchain surface rejected format {}; using {} with the same color encoding",
                                        uint32_t(requested),
                                        uint32_t(alternate));
                }
            }
        }
        if (status != VriResult_Success || !m_Handle)
        {
            if (m_Handle)
                m_Device.swap.DestroySwapChain(m_Handle);
            m_Handle = nullptr;
            return std::unexpected("Create swapchain failed: " + std::to_string(status));
        }
        m_Requested = size;
        if (auto refreshed = refreshExpected(); !refreshed)
        {
            m_Images.clear();
            m_Device.swap.DestroySwapChain(m_Handle);
            m_Handle = nullptr;
            m_Format = requested;
            return std::unexpected(refreshed.error());
        }
        m_FormatNegotiated = true;
        m_Rebuild          = false;
        return {};
    }

    Swapchain::~Swapchain()
    {
        if (m_Handle)
        {
            m_Device.waitIdleForDestruction();
            m_Images.clear();
            m_Device.swap.DestroySwapChain(m_Handle);
        }
    }

    std::expected<void, std::string> Swapchain::refreshExpected()
    {
        Extent extent = m_Requested;
        if (m_Device.swap.GetSwapChainExtent)
        {
            const auto status = m_Device.swap.GetSwapChainExtent(m_Handle, &extent.width, &extent.height);
            if (status != VriResult_Success)
                return std::unexpected("Get swapchain extent failed: " + std::to_string(status));
        }
        if (extent.empty())
            return std::unexpected("Swapchain returned an empty extent");
        uint32_t count  = 0;
        auto     status = m_Device.swap.GetSwapChainTextures(m_Handle, nullptr, &count);
        if (status != VriResult_Success || !count)
            return std::unexpected("Count swapchain images failed: " + std::to_string(status));
        std::vector<VriTexture*> images(count);
        status = m_Device.swap.GetSwapChainTextures(m_Handle, images.data(), &count);
        if (status != VriResult_Success || !count || count > images.size())
            return std::unexpected("Get swapchain images failed: " + std::to_string(status));
        std::vector<std::unique_ptr<Texture>> wrapped;
        wrapped.reserve(count);
        for (uint32_t i = 0; i < count; ++i)
        {
            if (!images[i])
                return std::unexpected("Swapchain returned a null image");
            auto texture = Texture::create(m_Device, colorTexture(extent, m_Format), images[i]);
            if (!texture)
                return std::unexpected("Wrap swapchain image: " + texture.error());
            wrapped.push_back(std::move(*texture));
        }
        m_Extent = extent;
        m_Images = std::move(wrapped);
        return {};
    }

    std::expected<bool, std::string> Swapchain::prepareExpected(Extent size)
    {
        if (!m_Handle)
        {
            if (auto created = createHandleExpected(); !created)
                return std::unexpected(created.error());
        }
        else if (m_Rebuild || size != m_Requested)
        {
            if (auto idle = m_Device.waitIdleExpected(); !idle)
                return std::unexpected(idle.error());
            m_Images.clear(); // Views must be destroyed before Resize destroys the images.
            const auto status = m_Device.swap.Resize(m_Handle, size.width, size.height);
            if (status == VriResult_OutOfDate)
            {
                m_Rebuild = true;
                return false;
            }
            if (status != VriResult_Success)
            {
                m_Rebuild = true;
                return std::unexpected("Resize swapchain failed: " + std::to_string(status));
            }
            m_Requested = size;
            if (auto refreshed = refreshExpected(); !refreshed)
            {
                m_Rebuild = true;
                return std::unexpected(refreshed.error());
            }
            m_Rebuild = false;
        }
        return true;
    }

    std::expected<void, std::string> Swapchain::resizeExpected(Extent size)
    {
        if (m_Acquired)
            return std::unexpected("Cannot resize a swapchain with an acquired image");
        m_NativeSize = size;
        if (!m_WindowSize && !size.empty())
        {
            auto prepared = prepareExpected(size);
            if (!prepared)
                return std::unexpected(prepared.error());
        }
        return {};
    }

    std::expected<Texture*, std::string> Swapchain::acquireExpected()
    {
        if (m_Acquired)
            return std::unexpected("Swapchain image is already acquired");
        auto currentSize = surfaceSizeExpected();
        if (!currentSize)
            return std::unexpected(currentSize.error());
        if (currentSize->empty())
            return nullptr;
        auto prepared = prepareExpected(*currentSize);
        if (!prepared)
            return std::unexpected(prepared.error());
        if (!*prepared)
            return nullptr;
        uint32_t   index  = 0;
        const auto status = m_Device.swap.AcquireNextTexture(m_Handle, nullptr, 0, &index);
        if (status == VriResult_OutOfDate)
        {
            m_Rebuild = true;
            return nullptr;
        }
        if (status != VriResult_Success)
            return std::unexpected("Acquire swapchain image failed: " + std::to_string(status));
        // The backend owns an acquired image even if subsequent wrapper validation fails.
        m_Acquired = true;
        if (index >= m_Images.size())
            return std::unexpected("Swapchain returned an invalid image index");
        if (m_Device.core.GetDeviceDesc(m_Device.handle)->graphicsAPI == VriGraphicsAPI_WebGPU)
        {
            // VRI reuses a handle for successive canvas images, but their views do not persist.
            auto replacement = Texture::create(m_Device, colorTexture(m_Extent, m_Format), m_Images[index]->handle);
            if (!replacement)
                return std::unexpected("Refresh browser swapchain image: " + replacement.error());
            m_Images[index] = std::move(*replacement);
        }
        return m_Images[index].get();
    }

    std::expected<void, std::string> Swapchain::presentExpected()
    {
        if (!m_Acquired)
            return std::unexpected("Cannot present without an acquired swapchain image");
        const auto status = m_Device.swap.Present(m_Handle, nullptr, 0);
        m_Acquired        = false;
        if (status == VriResult_OutOfDate)
        {
            m_Rebuild = true;
            return {};
        }
        if (status != VriResult_Success)
        {
            m_Rebuild = true;
            return std::unexpected("Present failed: " + std::to_string(status));
        }
        return {};
    }

    std::expected<void, std::string> Swapchain::setVsyncExpected(bool enabled)
    {
        const auto mode = enabled ? VriPresentMode_Fifo : VriPresentMode_Immediate;
        if (m_Acquired)
            return std::unexpected("Cannot change vsync with an acquired swapchain image");
        if (m_PresentMode == mode)
            return {};
        auto currentSize = surfaceSizeExpected();
        if (!currentSize)
            return std::unexpected(currentSize.error());
        if (currentSize->empty())
            return {};
        if (auto idle = m_Device.waitIdleExpected(); !idle)
            return std::unexpected(idle.error());
        const auto previous = m_PresentMode;
        m_Images.clear();
        if (m_Handle)
            m_Device.swap.DestroySwapChain(m_Handle);
        m_Handle      = nullptr;
        m_PresentMode = mode;
        if (auto created = createHandleExpected(); !created)
        {
            const auto error = created.error();
            m_PresentMode    = previous;
            if (auto restored = createHandleExpected(); !restored)
            {
                m_Rebuild = true;
                return std::unexpected(error + "; restoring previous swapchain failed: " + restored.error());
            }
            return std::unexpected(error);
        }
        return {};
    }

    void Swapchain::resize(Extent size)
    {
        if (auto result = resizeExpected(size); !result)
            throw std::runtime_error(result.error());
    }

    Texture* Swapchain::acquire()
    {
        auto result = acquireExpected();
        if (!result)
            throw std::runtime_error(result.error());
        return *result;
    }

    void Swapchain::present()
    {
        if (auto result = presentExpected(); !result)
            throw std::runtime_error(result.error());
    }

    void Swapchain::setVsync(bool enabled)
    {
        if (auto result = setVsyncExpected(enabled); !result)
            throw std::runtime_error(result.error());
    }
} // namespace vultra
