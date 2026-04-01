#include "vultra/core/rhi/swapchain.hpp"
#include "vultra/core/os/window.hpp"
#include "vultra/core/rhi/vk/macro.hpp"

#include "vultra/core/profiling/tracy_wrapper.hpp"

#include <magic_enum/magic_enum.hpp>

namespace vultra
{
    namespace rhi
    {
        struct VulkanSwapchainBackend
        {
            os::Window*            m_Window {nullptr};
            vk::Instance           m_Instance {nullptr};
            vk::PhysicalDevice     m_PhysicalDevice {nullptr};
            vk::Device             m_Device {nullptr};
            vk::SurfaceKHR         m_Surface {nullptr};
            vk::SwapchainKHR       m_Handle {nullptr};
            Swapchain::Format      m_Format {Swapchain::Format::eLinear};
            VerticalSync           m_VerticalSync {VerticalSync::eDisabled};
            std::vector<Texture>   m_Buffers;
            uint32_t               m_CurrentImageIndex {0};
        };

#define m_Window (m_Backend->m_Window)
#define m_Instance (m_Backend->m_Instance)
#define m_PhysicalDevice (m_Backend->m_PhysicalDevice)
#define m_Device (m_Backend->m_Device)
#define m_Surface (m_Backend->m_Surface)
#define m_Handle (m_Backend->m_Handle)
#define m_Format (m_Backend->m_Format)
#define m_VerticalSync (m_Backend->m_VerticalSync)
#define m_Buffers (m_Backend->m_Buffers)
#define m_CurrentImageIndex (m_Backend->m_CurrentImageIndex)

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

            [[nodiscard]] vk::SurfaceFormatKHR chooseSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& formats,
                                                                   const Swapchain::Format                  format)
            {
                const vk::SurfaceFormatKHR preferred {
                    format == Swapchain::Format::eLinear ? vk::Format::eB8G8R8A8Unorm : vk::Format::eB8G8R8A8Srgb,
                    vk::ColorSpaceKHR::eSrgbNonlinear,
                };

                const auto it = std::find(formats.begin(), formats.end(), preferred);
                if (it != formats.end())
                    return *it;

                return formats.empty() ? preferred : formats.front();
            }

            [[nodiscard]] vk::CompositeAlphaFlagBitsKHR
            chooseCompositeAlpha(const vk::SurfaceCapabilitiesKHR& capabilities)
            {
                constexpr vk::CompositeAlphaFlagBitsKHR candidates[] = {
                    vk::CompositeAlphaFlagBitsKHR::eOpaque,
                    vk::CompositeAlphaFlagBitsKHR::ePreMultiplied,
                    vk::CompositeAlphaFlagBitsKHR::ePostMultiplied,
                    vk::CompositeAlphaFlagBitsKHR::eInherit,
                };

                for (const auto candidate : candidates)
                {
                    if ((capabilities.supportedCompositeAlpha & candidate) == candidate)
                        return candidate;
                }

                return vk::CompositeAlphaFlagBitsKHR::eOpaque;
            }

            void logAndroidSwapchainTransformOverrideOnce(const vk::SurfaceTransformFlagBitsKHR currentTransform,
                                                          const vk::SurfaceTransformFlagBitsKHR preTransform)
            {
                static bool logged = false;
                if (logged)
                    return;

                logged = true;
                VULTRA_CORE_WARN("[Swapchain] Android surface reports currentTransform={}, but forcing preTransform={}."
                                 " Recreate diagnostics will be suppressed after this point.",
                                 vk::to_string(currentTransform),
                                 vk::to_string(preTransform));
            }
        } // namespace

        Swapchain::Swapchain(Swapchain&& other) noexcept : m_Backend(std::move(other.m_Backend)) {}

        Swapchain::~Swapchain() { destroy(); }

        Swapchain& Swapchain::operator=(Swapchain&& rhs) noexcept
        {
            if (this != &rhs)
            {
                destroy();
                std::swap(m_Backend, rhs.m_Backend);
            }

            return *this;
        }

        Swapchain::operator bool() const { return m_Backend && m_Handle != nullptr; }

        Swapchain::Format Swapchain::getFormat() const { return m_Backend ? m_Format : Format::eLinear; }

        PixelFormat Swapchain::getPixelFormat() const
        {
            return m_Backend && m_Handle ? m_Buffers.back().getPixelFormat() : PixelFormat::eUndefined;
        }

        Extent2D Swapchain::getExtent() const { return m_Backend && m_Handle ? m_Buffers.back().getExtent() : Extent2D {}; }

        std::size_t Swapchain::getNumBuffers() const { return m_Backend ? m_Buffers.size() : 0u; }

        std::uintptr_t Swapchain::getNativeHandle() const
        {
            return m_Backend ? reinterpret_cast<std::uintptr_t>(static_cast<VkSwapchainKHR>(m_Handle)) : 0u;
        }

        const std::vector<Texture>& Swapchain::getBuffers() const { return m_Buffers; }

        const Texture& Swapchain::getBuffer(const uint32_t i) const { return m_Buffers[i]; }

        uint32_t Swapchain::getCurrentBufferIndex() const { return m_Backend ? m_CurrentImageIndex : 0u; }

        Texture& Swapchain::getCurrentBuffer() { return m_Buffers[m_CurrentImageIndex]; }

        void Swapchain::recreate(const std::optional<VerticalSync> vsync)
        {
            assert(m_Backend);
            m_Buffers.clear();
            create(m_Format, vsync.value_or(m_VerticalSync));
        }

        bool Swapchain::acquireNextImage(const std::uintptr_t imageAcquired)
        {
            assert(m_Backend && m_Handle);
            ZoneScopedN("RHI::AcquireNextImage");

            const auto result = m_Device.acquireNextImageKHR(
                m_Handle,
                std::numeric_limits<uint64_t>::max(),
                vk::Semaphore {reinterpret_cast<VkSemaphore>(imageAcquired)},
                nullptr,
                &m_CurrentImageIndex);

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

        Swapchain::Swapchain(const std::uintptr_t instance,
                             const std::uintptr_t physicalDevice,
                             const std::uintptr_t device,
                             os::Window*          window,
                             const Format         format,
                             const VerticalSync   vsync) :
            m_Backend(std::make_shared<VulkanSwapchainBackend>())
        {
            m_Instance       = vk::Instance {reinterpret_cast<VkInstance>(instance)};
            m_PhysicalDevice = vk::PhysicalDevice {reinterpret_cast<VkPhysicalDevice>(physicalDevice)};
            m_Device         = vk::Device {reinterpret_cast<VkDevice>(device)};
            m_Window         = window;
            createSurface();
            create(format, vsync);
        }

        void Swapchain::createSurface()
        {
            assert(m_Backend);
            assert(m_Instance);
            VULTRA_CORE_ASSERT(m_Window != nullptr, "[Swapchain] Window must not be null.");
            VULTRA_CORE_TRACE("[Swapchain] Creating Vulkan surface for window driver {}",
                              magic_enum::enum_name(m_Window->driverType()));
            m_Surface = m_Window->createVulkanSurface(m_Instance);
            VULTRA_CORE_TRACE("[Swapchain] Vulkan surface created");
        }

        void Swapchain::create(Format format, VerticalSync vsync)
        {
            assert(m_Backend);
            m_Device.waitIdle();

            const auto oldSwapchain = std::exchange(m_Handle, nullptr);

            const auto surfaceInfo = getSurfaceInfo(m_PhysicalDevice, m_Surface);
            VULTRA_CORE_TRACE("[Swapchain] Surface info acquired");

            // Using framebuffer extent as the swapchain extent for Wayland compatibility
            const os::Window::Extent fbExtent = m_Window->getFrameBufferExtent();

            Extent2D extent;
            switch (m_Window->driverType())
            {
                case os::Window::DriverType::eAndroid:
                    if (surfaceInfo.capabilities.currentExtent.width != 4294967295u &&
                        surfaceInfo.capabilities.currentExtent.height != 4294967295u)
                    {
                        extent = fromVk(surfaceInfo.capabilities.currentExtent);
                    }
                    else
                    {
                        extent = Extent2D {static_cast<uint32_t>(fbExtent.x), static_cast<uint32_t>(fbExtent.y)};
                    }
                    break;

                case os::Window::DriverType::eX11:
                    if (surfaceInfo.capabilities.currentExtent.width != 4294967289u &&
                        surfaceInfo.capabilities.currentExtent.height != 4294967289u)
                    {
                        extent = fromVk(surfaceInfo.capabilities.currentExtent);
                    }
                    else
                    {
                        extent = Extent2D {static_cast<uint32_t>(fbExtent.x), static_cast<uint32_t>(fbExtent.y)};
                    }
                    break;

                case os::Window::DriverType::eWayland:
                    extent = Extent2D {static_cast<uint32_t>(fbExtent.x), static_cast<uint32_t>(fbExtent.y)};
                    break;

                default:
                    extent = fromVk(surfaceInfo.capabilities.currentExtent);
                    break;
            }

            extent.width  = std::clamp(extent.width,
                                      surfaceInfo.capabilities.minImageExtent.width,
                                      surfaceInfo.capabilities.maxImageExtent.width);
            extent.height = std::clamp(extent.height,
                                       surfaceInfo.capabilities.minImageExtent.height,
                                       surfaceInfo.capabilities.maxImageExtent.height);

            VULTRA_CORE_TRACE("[Swapchain] Using extent {}x{}, framebuffer {}x{}, currentExtent {}x{}",
                              extent.width,
                              extent.height,
                              fbExtent.x,
                              fbExtent.y,
                              surfaceInfo.capabilities.currentExtent.width,
                              surfaceInfo.capabilities.currentExtent.height);

            // Check if the present mode is supported
            auto presentMode = getPresentMode(vsync);
            if (std::find(surfaceInfo.presentModes.begin(), surfaceInfo.presentModes.end(), presentMode) ==
                surfaceInfo.presentModes.end())
            {
                VULTRA_CORE_WARN("[Swapchain] Requested present mode ({}) is not supported, falling back to FIFO",
                                 magic_enum::enum_name(presentMode));
                presentMode = vk::PresentModeKHR::eFifo;
            }

            const auto surfaceFormat = chooseSurfaceFormat(surfaceInfo.formats, format);
            const bool supportsIdentityTransform =
                (surfaceInfo.capabilities.supportedTransforms & vk::SurfaceTransformFlagBitsKHR::eIdentity) ==
                vk::SurfaceTransformFlagBitsKHR::eIdentity;
            const auto preTransform =
                (m_Window->driverType() == os::Window::DriverType::eAndroid && supportsIdentityTransform) ?
                    vk::SurfaceTransformFlagBitsKHR::eIdentity :
                (surfaceInfo.capabilities.supportedTransforms & surfaceInfo.capabilities.currentTransform) ==
                        surfaceInfo.capabilities.currentTransform ?
                    surfaceInfo.capabilities.currentTransform :
                    vk::SurfaceTransformFlagBitsKHR::eIdentity;
            const auto compositeAlpha = chooseCompositeAlpha(surfaceInfo.capabilities);

            vk::SwapchainCreateInfoKHR swapchainCreateInfo {};
            swapchainCreateInfo.surface = m_Surface;
            swapchainCreateInfo.minImageCount =
                std::clamp(3u,
                           surfaceInfo.capabilities.minImageCount,
                           surfaceInfo.capabilities.maxImageCount > 0 ? surfaceInfo.capabilities.maxImageCount : 8u);
            swapchainCreateInfo.imageFormat      = surfaceFormat.format;
            swapchainCreateInfo.imageColorSpace  = surfaceFormat.colorSpace;
            swapchainCreateInfo.imageExtent      = static_cast<vk::Extent2D>(extent);
            swapchainCreateInfo.imageArrayLayers = 1; // No Stereo
            swapchainCreateInfo.imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc |
                                             vk::ImageUsageFlagBits::eTransferDst |
                                             vk::ImageUsageFlagBits::eColorAttachment;
            swapchainCreateInfo.imageSharingMode = vk::SharingMode::eExclusive;
            swapchainCreateInfo.preTransform     = preTransform;
            swapchainCreateInfo.compositeAlpha   = compositeAlpha;
            swapchainCreateInfo.presentMode      = presentMode;
            swapchainCreateInfo.clipped          = true;
            swapchainCreateInfo.oldSwapchain     = oldSwapchain;

            const bool androidTransformOverride = m_Window->driverType() == os::Window::DriverType::eAndroid &&
                                                  preTransform != surfaceInfo.capabilities.currentTransform;

            if (androidTransformOverride)
            {
                logAndroidSwapchainTransformOverrideOnce(surfaceInfo.capabilities.currentTransform, preTransform);
            }
            else
            {
                VULTRA_CORE_TRACE("[Swapchain] Creating swapchain format={}, colorSpace={}, currentTransform={}, "
                                  "preTransform={}, compositeAlpha={}, minImageCount={}",
                                  vk::to_string(surfaceFormat.format),
                                  vk::to_string(surfaceFormat.colorSpace),
                                  vk::to_string(surfaceInfo.capabilities.currentTransform),
                                  vk::to_string(preTransform),
                                  vk::to_string(compositeAlpha),
                                  swapchainCreateInfo.minImageCount);
            }

            VK_CHECK(m_Device.createSwapchainKHR(&swapchainCreateInfo, nullptr, &m_Handle),
                     "Swapchain",
                     "Failed to create swapchain");

            buildBuffers(extent, fromVk(swapchainCreateInfo.imageFormat));
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
            assert(m_Backend);
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
            if (!m_Backend)
                return;

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

            m_Window  = nullptr;
            m_Surface = nullptr;

            m_Instance       = nullptr;
            m_PhysicalDevice = nullptr;
            m_Device         = nullptr;

            m_CurrentImageIndex = 0;
        }

#undef m_Window
#undef m_Instance
#undef m_PhysicalDevice
#undef m_Device
#undef m_Surface
#undef m_Handle
#undef m_Format
#undef m_VerticalSync
#undef m_Buffers
#undef m_CurrentImageIndex

        Rect2D getRenderArea(const Swapchain& swapchain)
        {
            return Rect2D {.offset = {0, 0}, .extent = {swapchain.getExtent()}};
        }
    } // namespace rhi
} // namespace vultra
