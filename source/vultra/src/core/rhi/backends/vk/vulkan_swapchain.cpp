#include "vultra/core/rhi/interfaces/iswapchain.hpp"

#include "vultra/core/os/window.hpp"
#include "vultra/core/profiling/tracy_wrapper.hpp"
#include "vultra/core/rhi/backends/vk/conversions.hpp"
#include "vultra/core/rhi/backends/vk/handle_utils.hpp"
#include "vultra/core/rhi/backends/vk/macro.hpp"
#include "vultra/core/rhi/backends/vk/vulkan_swapchain.hpp"
#include "vultra/core/rhi/interfaces/texture_access.hpp"

#include <algorithm>
#include <limits>
#include <magic_enum/magic_enum.hpp>

namespace vultra
{
    namespace rhi
    {
        namespace
        {
            struct SurfaceInfo
            {
                vk::SurfaceCapabilitiesKHR         capabilities;
                std::vector<vk::SurfaceFormatKHR> formats;
                std::vector<vk::PresentModeKHR>   presentModes;
            };

            [[nodiscard]] SurfaceInfo getSurfaceInfo(const vk::PhysicalDevice physicalDevice, const vk::SurfaceKHR surface)
            {
                assert(physicalDevice && surface);

                SurfaceInfo surfaceInfo {};
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
                VK_CHECK(physicalDevice.getSurfacePresentModesKHR(surface, &numPresentModes, surfaceInfo.presentModes.data()),
                         "Swapchain",
                         "Failed to get surface presentation modes");

                return surfaceInfo;
            }

            [[nodiscard]] Extent2D fromVkExtent(const vk::Extent2D extent) { return {extent.width, extent.height}; }

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
                                                                   const SwapchainFormat                    format)
            {
                const vk::SurfaceFormatKHR preferred {
                    format == SwapchainFormat::eLinear ? vk::Format::eB8G8R8A8Unorm : vk::Format::eB8G8R8A8Srgb,
                    vk::ColorSpaceKHR::eSrgbNonlinear,
                };

                const auto it = std::find(formats.begin(), formats.end(), preferred);
                if (it != formats.end())
                {
                    return *it;
                }
                return formats.empty() ? preferred : formats.front();
            }

            [[nodiscard]] vk::CompositeAlphaFlagBitsKHR chooseCompositeAlpha(const vk::SurfaceCapabilitiesKHR& capabilities)
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
                    {
                        return candidate;
                    }
                }
                return vk::CompositeAlphaFlagBitsKHR::eOpaque;
            }
        } // namespace

        VulkanSwapchain::VulkanSwapchain(const std::uintptr_t instance,
                                                       const std::uintptr_t physicalDevice,
                                                       const std::uintptr_t device,
                                                       os::Window*          window,
                                                       const SwapchainFormat format,
                                                       const VerticalSync   vsync)
        {
            m_Instance       = vk::Instance {asVkHandle<VkInstance>(instance)};
            m_PhysicalDevice = vk::PhysicalDevice {asVkHandle<VkPhysicalDevice>(physicalDevice)};
            m_Device         = vk::Device {asVkHandle<VkDevice>(device)};
            m_Window         = window;
            createSurface();
            createSwapchain(format, vsync);
        }

        VulkanSwapchain::~VulkanSwapchain() { destroy(); }

        bool VulkanSwapchain::isValid() const { return m_Handle != nullptr; }

        SwapchainFormat VulkanSwapchain::getFormat() const { return m_Format; }

        PixelFormat VulkanSwapchain::getPixelFormat() const
        {
            assert(m_Handle && !m_Buffers.empty());
            return m_Buffers.back().getPixelFormat();
        }

        Extent2D VulkanSwapchain::getExtent() const
        {
            assert(m_Handle && !m_Buffers.empty());
            return m_Buffers.back().getExtent();
        }

        std::size_t VulkanSwapchain::getNumBuffers() const { return m_Buffers.size(); }

        std::uintptr_t VulkanSwapchain::getHandle() const
        {
            assert(m_Handle);
            return toBackendHandle(static_cast<VkSwapchainKHR>(m_Handle));
        }

        const std::vector<Texture>& VulkanSwapchain::getBuffers() const { return m_Buffers; }

        const Texture& VulkanSwapchain::getBuffer(const uint32_t index) const { return m_Buffers[index]; }

        uint32_t VulkanSwapchain::getCurrentBufferIndex() const { return m_CurrentImageIndex; }

        Texture& VulkanSwapchain::getCurrentBuffer() { return m_Buffers[m_CurrentImageIndex]; }

        void VulkanSwapchain::recreate(const std::optional<VerticalSync> vsync)
        {
            m_Buffers.clear();
            createSwapchain(m_Format, vsync.value_or(m_VerticalSync));
        }

        bool VulkanSwapchain::acquireNextImage(const std::uintptr_t imageAcquired)
        {
            assert(m_Handle);
            ZoneScopedN("RHI::AcquireNextImage");

            const auto result = m_Device.acquireNextImageKHR(
                m_Handle,
                std::numeric_limits<uint64_t>::max(),
                vk::Semaphore {asVkHandle<VkSemaphore>(imageAcquired)},
                nullptr,
                &m_CurrentImageIndex);

            switch (result)
            {
                case vk::Result::eErrorOutOfDateKHR:
                    recreate(std::nullopt);
                    [[fallthrough]];
                case vk::Result::eSuboptimalKHR:
                case vk::Result::eSuccess:
                    if (m_CurrentImageIndex < m_Buffers.size())
                    {
                        // A presented swapchain image is only used as a fresh render target after acquire.
                        // Discarding the previous contents avoids carrying a stale tracked layout into the next frame.
                        m_Buffers[m_CurrentImageIndex].setBarrierState({}, ImageLayout::eUndefined);
                    }
                    return true;
                default:
                    assert(false);
                    return false;
            }
        }

        void VulkanSwapchain::createSurface()
        {
            assert(m_Instance);
            VULTRA_CORE_ASSERT(m_Window != nullptr, "[Swapchain] Window must not be null.");
            VULTRA_CORE_TRACE("[Swapchain] Creating Vulkan surface for window driver {}",
                              magic_enum::enum_name(m_Window->driverType()));
            m_Surface = m_Window->createVulkanSurface(m_Instance);
            VULTRA_CORE_TRACE("[Swapchain] Vulkan surface created");
        }

        void VulkanSwapchain::buildBuffers(const Extent2D extent, const PixelFormat pixelFormat)
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
            for (const auto image : swapchainImages)
            {
                m_Buffers.emplace_back(TextureAccess::fromExternalImage(
                    RenderBackendApi::eVulkan,
                    TextureDeviceHandle {toBackendHandle(static_cast<VkDevice>(m_Device))},
                    TextureImageHandle {toBackendHandle(static_cast<VkImage>(image))},
                    extent,
                    pixelFormat));
            }
        }

        void VulkanSwapchain::createSwapchain(const SwapchainFormat format, const VerticalSync vsync)
        {
            m_Device.waitIdle();
            const auto oldSwapchain = std::exchange(m_Handle, nullptr);

            const auto surfaceInfo = getSurfaceInfo(m_PhysicalDevice, m_Surface);
            VULTRA_CORE_TRACE("[Swapchain] Surface info acquired");

            const os::Window::Extent fbExtent = m_Window->getFrameBufferExtent();
            const bool variableExtent = surfaceInfo.capabilities.currentExtent.width == std::numeric_limits<uint32_t>::max() ||
                                        surfaceInfo.capabilities.currentExtent.height == std::numeric_limits<uint32_t>::max();
            Extent2D extent = variableExtent ? Extent2D {static_cast<uint32_t>(fbExtent.x), static_cast<uint32_t>(fbExtent.y)} :
                                               fromVkExtent(surfaceInfo.capabilities.currentExtent);

            extent.width = std::clamp(extent.width,
                                      surfaceInfo.capabilities.minImageExtent.width,
                                      surfaceInfo.capabilities.maxImageExtent.width);
            extent.height = std::clamp(extent.height,
                                       surfaceInfo.capabilities.minImageExtent.height,
                                       surfaceInfo.capabilities.maxImageExtent.height);

            auto presentMode = getPresentMode(vsync);
            if (std::find(surfaceInfo.presentModes.begin(), surfaceInfo.presentModes.end(), presentMode) ==
                surfaceInfo.presentModes.end())
            {
                VULTRA_CORE_WARN("[Swapchain] Requested present mode ({}) is not supported, falling back to FIFO",
                                 magic_enum::enum_name(presentMode));
                presentMode = vk::PresentModeKHR::eFifo;
            }

            const auto surfaceFormat  = chooseSurfaceFormat(surfaceInfo.formats, format);
            const auto preTransform   = (surfaceInfo.capabilities.supportedTransforms & surfaceInfo.capabilities.currentTransform) ==
                                            surfaceInfo.capabilities.currentTransform ?
                                            surfaceInfo.capabilities.currentTransform :
                                            vk::SurfaceTransformFlagBitsKHR::eIdentity;
            const auto compositeAlpha = chooseCompositeAlpha(surfaceInfo.capabilities);

            vk::SwapchainCreateInfoKHR createInfo {};
            createInfo.surface         = m_Surface;
            createInfo.minImageCount   = std::clamp(3u,
                                                  surfaceInfo.capabilities.minImageCount,
                                                  surfaceInfo.capabilities.maxImageCount > 0 ? surfaceInfo.capabilities.maxImageCount : 8u);
            createInfo.imageFormat      = surfaceFormat.format;
            createInfo.imageColorSpace  = surfaceFormat.colorSpace;
            createInfo.imageExtent      = toVk(extent);
            createInfo.imageArrayLayers = 1;
            createInfo.imageUsage       = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc |
                                    vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eColorAttachment;
            createInfo.imageSharingMode = vk::SharingMode::eExclusive;
            createInfo.preTransform     = preTransform;
            createInfo.compositeAlpha   = compositeAlpha;
            createInfo.presentMode      = presentMode;
            createInfo.clipped          = true;
            createInfo.oldSwapchain     = oldSwapchain;

            VK_CHECK(m_Device.createSwapchainKHR(&createInfo, nullptr, &m_Handle), "Swapchain", "Failed to create swapchain");

            buildBuffers(extent, fromVk(createInfo.imageFormat));
            m_Format       = format;
            m_VerticalSync = vsync;

            if (oldSwapchain)
            {
                m_Device.waitIdle();
                m_Device.destroySwapchainKHR(oldSwapchain, nullptr);
            }
            m_Device.waitIdle();
        }

        void VulkanSwapchain::destroy()
        {
            m_Buffers.clear();

            if (m_Handle)
            {
                m_Device.waitIdle();
                m_Device.destroySwapchainKHR(m_Handle, nullptr);
                m_Handle = nullptr;
            }

            if (m_Surface)
            {
                m_Instance.destroySurfaceKHR(m_Surface, nullptr);
                m_Surface = nullptr;
            }

            m_Window            = nullptr;
            m_Instance          = nullptr;
            m_PhysicalDevice    = nullptr;
            m_Device            = nullptr;
            m_CurrentImageIndex = 0;
        }

    } // namespace rhi
} // namespace vultra
