#include "vultra/core/rhi/swapchain.hpp"
#include "vultra/core/os/window.hpp"
#include "vultra/core/rhi/vk/macro.hpp"

#include <magic_enum/magic_enum.hpp>
#include <tracy/Tracy.hpp>

namespace vultra
{
    namespace rhi
    {
        namespace
        {
            struct SurfaceInfo
            {
                vk::SurfaceCapabilitiesKHR        capabilities;
                std::vector<vk::SurfaceFormatKHR> formats;
                std::vector<vk::PresentModeKHR>   presentModes;
            };

            [[nodiscard]] auto getSurfaceInfo(const vk::PhysicalDevice physicalDevice, const vk::SurfaceKHR surface)
            {
                assert(physicalDevice && surface);

                SurfaceInfo surfaceInfo;
                VK_CHECK(physicalDevice.getSurfaceCapabilitiesKHR(surface, &surfaceInfo.capabilities),
                         "Swapchain",
                         "Failed to get surface capabilities");

                uint32_t numFormats {0};
                VK_CHECK(physicalDevice.getSurfaceFormatsKHR(surface, &numFormats, nullptr),
                         "Swapchain",
                         "Failed to get surface formats");

                surfaceInfo.formats.resize(numFormats);
                VK_CHECK(physicalDevice.getSurfaceFormatsKHR(surface, &numFormats, surfaceInfo.formats.data()),
                         "Swapchain",
                         "Failed to get surface formats");

                uint32_t numPresentModes {0};
                VK_CHECK(physicalDevice.getSurfacePresentModesKHR(surface, &numPresentModes, nullptr),
                         "Swapchain",
                         "Failed to get surface presentation modes");

                surfaceInfo.presentModes.resize(numPresentModes);
                VK_CHECK(physicalDevice.getSurfacePresentModesKHR(
                             surface, &numPresentModes, surfaceInfo.presentModes.data()),
                         "Swapchain",
                         "Failed to get surface presentation modes");

                return surfaceInfo;
            }

            [[nodiscard]] Extent2D fromVk(const vk::Extent2D extent) { return {extent.width, extent.height}; }

            [[nodiscard]] vk::PresentModeKHR getPresentMode(const VerticalSync vsync)
            {
                switch (vsync)
                {
                    using enum VerticalSync;

                    case eDisabled:
                        return vk::PresentModeKHR::eImmediate;
                    case eEnabled:
                        return vk::PresentModeKHR::eFifo;
                    case eAdaptive:
                        return vk::PresentModeKHR::eMailbox;
                }

                assert(false);
                return vk::PresentModeKHR::eImmediate;
            }
        } // namespace

        Swapchain::Swapchain(Swapchain&& other) noexcept :
            m_Window(other.m_Window), m_Instance(other.m_Instance), m_PhysicalDevice(other.m_PhysicalDevice),
            m_Device(other.m_Device), m_Surface(other.m_Surface), m_Handle(other.m_Handle), m_Format(other.m_Format),
            m_VerticalSync(other.m_VerticalSync), m_Buffers(std::move(other.m_Buffers)),
            m_CurrentImageIndex(other.m_CurrentImageIndex)
        {
            other.m_Window            = nullptr;
            other.m_Instance          = nullptr;
            other.m_PhysicalDevice    = nullptr;
            other.m_Device            = nullptr;
            other.m_Surface           = nullptr;
            other.m_Handle            = nullptr;
            other.m_CurrentImageIndex = 0;
        }

        Swapchain::~Swapchain() { destroy(); }

        Swapchain& Swapchain::operator=(Swapchain&& rhs) noexcept
        {
            if (this != &rhs)
            {
                destroy();

                std::swap(m_Window, rhs.m_Window);
                std::swap(m_Instance, rhs.m_Instance);
                std::swap(m_PhysicalDevice, rhs.m_PhysicalDevice);
                std::swap(m_Device, rhs.m_Device);
                std::swap(m_Surface, rhs.m_Surface);
                std::swap(m_Handle, rhs.m_Handle);
                std::swap(m_Format, rhs.m_Format);
                std::swap(m_VerticalSync, rhs.m_VerticalSync);
                std::swap(m_Buffers, rhs.m_Buffers);
                std::swap(m_CurrentImageIndex, rhs.m_CurrentImageIndex);
            }

            return *this;
        }

        Swapchain::operator bool() const { return m_Handle != nullptr; }

        Swapchain::Format Swapchain::getFormat() const { return m_Format; }

        PixelFormat Swapchain::getPixelFormat() const
        {
            return m_Handle ? m_Buffers.back().getPixelFormat() : PixelFormat::eUndefined;
        }

        Extent2D Swapchain::getExtent() const { return m_Handle ? m_Buffers.back().getExtent() : Extent2D {}; }

        std::size_t Swapchain::getNumBuffers() const { return m_Buffers.size(); }

        const std::vector<Texture>& Swapchain::getBuffers() const { return m_Buffers; }

        const Texture& Swapchain::getBuffer(const uint32_t i) const { return m_Buffers[i]; }

        uint32_t Swapchain::getCurrentBufferIndex() const { return m_CurrentImageIndex; }

        Texture& Swapchain::getCurrentBuffer() { return m_Buffers[m_CurrentImageIndex]; }

        void Swapchain::recreate(const std::optional<VerticalSync> vsync)
        {
            m_Buffers.clear();
            create(m_Format, vsync.value_or(m_VerticalSync));
        }

        bool Swapchain::acquireNextImage(const vk::Semaphore imageAcquired)
        {
            assert(m_Handle);
            ZoneScopedN("RHI::AcquireNextImage");

            const auto result = m_Device.acquireNextImageKHR(
                m_Handle, std::numeric_limits<uint64_t>::max(), imageAcquired, nullptr, &m_CurrentImageIndex);

            switch (result)
            {
                case vk::Result::eErrorOutOfDateKHR:
                    recreate();
                    [[fallthrough]];
                case vk::Result::eSuboptimalKHR:
                case vk::Result::eSuccess:
                    return true;

                default:
                    assert(false);
                    return false;
            }
        }

        Swapchain::Swapchain(const vk::Instance       instance,
                             const vk::PhysicalDevice physicalDevice,
                             const vk::Device         device,
                             os::Window*              window,
                             const Format             format,
                             const VerticalSync       vsync) :
            m_Instance(instance), m_PhysicalDevice(physicalDevice), m_Device(device), m_Window(window)
        {
            createSurface();
            create(format, vsync);
        }

        void Swapchain::createSurface()
        {
            assert(m_Instance);
            m_Surface = m_Window->createVulkanSurface(m_Instance);
        }

        void Swapchain::create(Format format, VerticalSync vsync)
        {
            m_Device.waitIdle();

            const auto oldSwapchain = std::exchange(m_Handle, nullptr);

            const auto surfaceInfo = getSurfaceInfo(m_PhysicalDevice, m_Surface);

            // Using framebuffer extent as the swapchain extent for Wayland compatibility
            const auto fbExtent = m_Window->getFrameBufferExtent();

            Extent2D extent;

            switch (m_Window->getDriverType())
            {
                case os::Window::DriverType::eX11:
                    if (surfaceInfo.capabilities.currentExtent.width != 4294967289u &&
                        surfaceInfo.capabilities.currentExtent.height != 4294967289u)
                    {
                        // Use the current extent if available
                        extent = fromVk(surfaceInfo.capabilities.currentExtent);
                    }
                    else
                    {
                        // Fallback to framebuffer extent
                        extent = Extent2D {static_cast<uint32_t>(fbExtent.x), static_cast<uint32_t>(fbExtent.y)};
                    }
                    break;

                case os::Window::DriverType::eWayland:
                    // Wayland does not provide a current extent, so we use the window extent
                    extent = Extent2D {static_cast<uint32_t>(fbExtent.x), static_cast<uint32_t>(fbExtent.y)};
                    break;

                default:
                    extent = fromVk(surfaceInfo.capabilities.currentExtent);
            }

            const vk::SurfaceFormatKHR kSurfaceDefaultFormat {
                format == Format::eLinear ? vk::Format::eB8G8R8A8Unorm : vk::Format::eB8G8R8A8Srgb,
                vk::ColorSpaceKHR::eSrgbNonlinear,
            };

            // Check if the present mode is supported
            auto presentMode = getPresentMode(vsync);
            if (std::find(surfaceInfo.presentModes.begin(), surfaceInfo.presentModes.end(), presentMode) ==
                surfaceInfo.presentModes.end())
            {
                VULTRA_CORE_WARN("[Swapchain] Requested present mode ({}) is not supported, falling back to FIFO",
                                 magic_enum::enum_name(presentMode));
                presentMode = vk::PresentModeKHR::eFifo;
            }

            vk::SwapchainCreateInfoKHR swapchainCreateInfo {};
            swapchainCreateInfo.surface = m_Surface;
            swapchainCreateInfo.minImageCount =
                std::clamp(3u,
                           surfaceInfo.capabilities.minImageCount,
                           surfaceInfo.capabilities.maxImageCount > 0 ? surfaceInfo.capabilities.maxImageCount : 8u);
            swapchainCreateInfo.imageFormat      = kSurfaceDefaultFormat.format;
            swapchainCreateInfo.imageColorSpace  = kSurfaceDefaultFormat.colorSpace;
            swapchainCreateInfo.imageExtent      = static_cast<vk::Extent2D>(extent);
            swapchainCreateInfo.imageArrayLayers = 1; // No Stereo
            swapchainCreateInfo.imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc |
                                             vk::ImageUsageFlagBits::eTransferDst |
                                             vk::ImageUsageFlagBits::eColorAttachment;
            swapchainCreateInfo.imageSharingMode = vk::SharingMode::eExclusive;
            swapchainCreateInfo.preTransform     = vk::SurfaceTransformFlagBitsKHR::eIdentity;
            swapchainCreateInfo.compositeAlpha   = vk::CompositeAlphaFlagBitsKHR::eOpaque;
            swapchainCreateInfo.presentMode      = presentMode;
            swapchainCreateInfo.clipped          = true;
            swapchainCreateInfo.oldSwapchain     = oldSwapchain;

            VK_CHECK(m_Device.createSwapchainKHR(&swapchainCreateInfo, nullptr, &m_Handle),
                     "Swapchain",
                     "Failed to create swapchain");

            buildBuffers(extent, static_cast<PixelFormat>(swapchainCreateInfo.imageFormat));
            m_Format       = format;
            m_VerticalSync = vsync;

            if (oldSwapchain)
            {
                m_Device.waitIdle();
                m_Device.destroySwapchainKHR(oldSwapchain);
            }

            m_Device.waitIdle();

            VULTRA_CORE_TRACE("[Swapchain] Created, extent: ({}, {}), present mode: {}",
                              extent.width,
                              extent.height,
                              magic_enum::enum_name(presentMode));
        }

        void Swapchain::buildBuffers(Extent2D extent, PixelFormat pixelFormat)
        {
            assert(m_Buffers.empty());

            uint32_t imageCount {0};
            VK_CHECK(m_Device.getSwapchainImagesKHR(m_Handle, &imageCount, nullptr),
                     "Swapchain",
                     "Failed to get swapchain images");

            std::vector<vk::Image> swapchainImages(imageCount);
            VK_CHECK(m_Device.getSwapchainImagesKHR(m_Handle, &imageCount, swapchainImages.data()),
                     "Swapchain",
                     "Failed to get swapchain images");

            m_Buffers.reserve(imageCount);
            for (auto image : swapchainImages)
            {
                m_Buffers.emplace_back(Texture {m_Device, image, extent, pixelFormat});
            }
        }

        void Swapchain::destroy()
        {
            m_Buffers.clear();

            if (m_Handle)
            {
                m_Device.waitIdle();
                m_Device.destroySwapchainKHR(m_Handle);
                m_Handle = nullptr;
            }

            if (m_Surface)
            {
                m_Instance.destroySurfaceKHR(m_Surface);
                m_Surface = nullptr;
            }

            m_Window = nullptr;

            m_Instance       = nullptr;
            m_PhysicalDevice = nullptr;
            m_Device         = nullptr;

            m_CurrentImageIndex = 0;
        }

        Rect2D getRenderArea(const Swapchain& swapchain)
        {
            return Rect2D {.offset = {0, 0}, .extent = {swapchain.getExtent()}};
        }
    } // namespace rhi
} // namespace vultra