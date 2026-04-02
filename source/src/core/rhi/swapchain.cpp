#include "vultra/core/rhi/swapchain.hpp"
#include "vultra/core/os/window.hpp"
#include "vultra/core/rhi/backends/vk/conversions.hpp"
#include "vultra/core/rhi/backends/vk/vulkan_swapchain_backend.hpp"
#include "vultra/core/rhi/backends/vk/macro.hpp"

#include "vultra/core/profiling/tracy_wrapper.hpp"

#include <magic_enum/magic_enum.hpp>

namespace vultra
{
    namespace rhi
    {
        namespace
        {
            [[nodiscard]] VulkanSwapchainBackend& backendOf(std::shared_ptr<ISwapchainBackend>& backend)
            {
                assert(backend);
                return *static_cast<VulkanSwapchainBackend*>(backend.get());
            }

            [[nodiscard]] const VulkanSwapchainBackend& backendOf(const std::shared_ptr<ISwapchainBackend>& backend)
            {
                assert(backend);
                return *static_cast<const VulkanSwapchainBackend*>(backend.get());
            }

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
                                                                   const SwapchainFormat                    format)
            {
                const vk::SurfaceFormatKHR preferred {
                    format == SwapchainFormat::eLinear ? vk::Format::eB8G8R8A8Unorm : vk::Format::eB8G8R8A8Srgb,
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

        Swapchain::operator bool() const
        {
            if (!m_Backend)
                return false;
            const auto& backend = backendOf(m_Backend);
            return backend.m_Handle != nullptr;
        }

        SwapchainFormat Swapchain::getFormat() const
        {
            const auto& backend = backendOf(m_Backend);
            return backend.m_Format;
        }

        PixelFormat Swapchain::getPixelFormat() const
        {
            const auto& backend = backendOf(m_Backend);
            assert(backend.m_Handle && !backend.m_Buffers.empty());
            return backend.m_Buffers.back().getPixelFormat();
        }

        Extent2D Swapchain::getExtent() const
        {
            const auto& backend = backendOf(m_Backend);
            assert(backend.m_Handle && !backend.m_Buffers.empty());
            return backend.m_Buffers.back().getExtent();
        }

        std::size_t Swapchain::getNumBuffers() const
        {
            const auto& backend = backendOf(m_Backend);
            return backend.m_Buffers.size();
        }

        std::uintptr_t Swapchain::getNativeHandle() const
        {
            const auto& backend = backendOf(m_Backend);
            assert(backend.m_Handle);
            return reinterpret_cast<std::uintptr_t>(static_cast<VkSwapchainKHR>(backend.m_Handle));
        }

        const std::vector<Texture>& Swapchain::getBuffers() const
        {
            const auto& backend = backendOf(m_Backend);
            return backend.m_Buffers;
        }

        const Texture& Swapchain::getBuffer(const uint32_t i) const
        {
            const auto& backend = backendOf(m_Backend);
            return backend.m_Buffers[i];
        }

        uint32_t Swapchain::getCurrentBufferIndex() const
        {
            const auto& backend = backendOf(m_Backend);
            return backend.m_CurrentImageIndex;
        }

        Texture& Swapchain::getCurrentBuffer()
        {
            auto& backend = backendOf(m_Backend);
            return backend.m_Buffers[backend.m_CurrentImageIndex];
        }

        void Swapchain::recreate(const std::optional<VerticalSync> vsync)
        {
            auto& backend = backendOf(m_Backend);
            backend.m_Buffers.clear();
            create(backend.m_Format, vsync.value_or(backend.m_VerticalSync));
        }

        bool Swapchain::acquireNextImage(const std::uintptr_t imageAcquired)
        {
            auto& backend = backendOf(m_Backend);
            assert(backend.m_Handle);
            ZoneScopedN("RHI::AcquireNextImage");

            const auto result = backend.m_Device.acquireNextImageKHR(
                backend.m_Handle,
                std::numeric_limits<uint64_t>::max(),
                vk::Semaphore {reinterpret_cast<VkSemaphore>(imageAcquired)},
                nullptr,
                &backend.m_CurrentImageIndex);

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
                             const SwapchainFormat format,
                             const VerticalSync   vsync) :
            m_Backend(std::make_shared<VulkanSwapchainBackend>())
        {
            auto& backend         = backendOf(m_Backend);
            backend.m_Instance       = vk::Instance {reinterpret_cast<VkInstance>(instance)};
            backend.m_PhysicalDevice = vk::PhysicalDevice {reinterpret_cast<VkPhysicalDevice>(physicalDevice)};
            backend.m_Device         = vk::Device {reinterpret_cast<VkDevice>(device)};
            backend.m_Window         = window;
            createSurface();
            create(format, vsync);
        }

        void Swapchain::createSurface()
        {
            auto& backend = backendOf(m_Backend);
            assert(backend.m_Instance);
            VULTRA_CORE_ASSERT(backend.m_Window != nullptr, "[Swapchain] Window must not be null.");
            VULTRA_CORE_TRACE("[Swapchain] Creating Vulkan surface for window driver {}",
                              magic_enum::enum_name(backend.m_Window->driverType()));
            backend.m_Surface = backend.m_Window->createVulkanSurface(backend.m_Instance);
            VULTRA_CORE_TRACE("[Swapchain] Vulkan surface created");
        }

        void Swapchain::create(SwapchainFormat format, VerticalSync vsync)
        {
            auto& backend = backendOf(m_Backend);
            backend.m_Device.waitIdle();

            const auto oldSwapchain = std::exchange(backend.m_Handle, nullptr);

            const auto surfaceInfo = getSurfaceInfo(backend.m_PhysicalDevice, backend.m_Surface);
            VULTRA_CORE_TRACE("[Swapchain] Surface info acquired");

            const os::Window::Extent fbExtent = backend.m_Window->getFrameBufferExtent();

            const bool variableExtent = surfaceInfo.capabilities.currentExtent.width == std::numeric_limits<uint32_t>::max() ||
                                        surfaceInfo.capabilities.currentExtent.height == std::numeric_limits<uint32_t>::max();
            Extent2D extent = variableExtent ? Extent2D {static_cast<uint32_t>(fbExtent.x), static_cast<uint32_t>(fbExtent.y)} :
                                               fromVk(surfaceInfo.capabilities.currentExtent);

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
            const auto preTransform =
                (surfaceInfo.capabilities.supportedTransforms & surfaceInfo.capabilities.currentTransform) ==
                        surfaceInfo.capabilities.currentTransform ?
                    surfaceInfo.capabilities.currentTransform :
                    vk::SurfaceTransformFlagBitsKHR::eIdentity;
            const auto compositeAlpha = chooseCompositeAlpha(surfaceInfo.capabilities);

            vk::SwapchainCreateInfoKHR swapchainCreateInfo {};
            swapchainCreateInfo.surface = backend.m_Surface;
            swapchainCreateInfo.minImageCount =
                std::clamp(3u,
                           surfaceInfo.capabilities.minImageCount,
                           surfaceInfo.capabilities.maxImageCount > 0 ? surfaceInfo.capabilities.maxImageCount : 8u);
            swapchainCreateInfo.imageFormat      = surfaceFormat.format;
            swapchainCreateInfo.imageColorSpace  = surfaceFormat.colorSpace;
            swapchainCreateInfo.imageExtent      = toVk(extent);
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

            VULTRA_CORE_TRACE("[Swapchain] Creating swapchain format={}, colorSpace={}, currentTransform={}, "
                              "preTransform={}, compositeAlpha={}, minImageCount={}",
                              vk::to_string(surfaceFormat.format),
                              vk::to_string(surfaceFormat.colorSpace),
                              vk::to_string(surfaceInfo.capabilities.currentTransform),
                              vk::to_string(preTransform),
                              vk::to_string(compositeAlpha),
                              swapchainCreateInfo.minImageCount);

            VK_CHECK(backend.m_Device.createSwapchainKHR(&swapchainCreateInfo, nullptr, &backend.m_Handle),
                     "Swapchain",
                     "Failed to create swapchain");

            buildBuffers(extent, fromVk(swapchainCreateInfo.imageFormat));
            backend.m_Format       = format;
            backend.m_VerticalSync = vsync;

            if (oldSwapchain)
            {
                backend.m_Device.waitIdle();
                backend.m_Device.destroySwapchainKHR(oldSwapchain, nullptr);
            }

            backend.m_Device.waitIdle();

            VULTRA_CORE_TRACE("[Swapchain] Created, extent: ({}, {}), present mode: {}",
                              extent.width,
                              extent.height,
                              magic_enum::enum_name(presentMode));
        }

        void Swapchain::buildBuffers(Extent2D extent, PixelFormat pixelFormat)
        {
            auto& backend = backendOf(m_Backend);
            assert(backend.m_Buffers.empty());

            uint32_t imageCount {0};
            VK_CHECK(backend.m_Device.getSwapchainImagesKHR(backend.m_Handle, &imageCount, nullptr),
                     "Swapchain",
                     "Failed to get swapchain images");

            std::vector<vk::Image> swapchainImages(imageCount);
            VK_CHECK(backend.m_Device.getSwapchainImagesKHR(backend.m_Handle, &imageCount, swapchainImages.data()),
                     "Swapchain",
                     "Failed to get swapchain images");

            backend.m_Buffers.reserve(imageCount);
            for (auto image : swapchainImages)
            {
                backend.m_Buffers.emplace_back(Texture {
                    reinterpret_cast<std::uintptr_t>(static_cast<VkDevice>(backend.m_Device)),
                    reinterpret_cast<std::uintptr_t>(static_cast<VkImage>(image)),
                    extent,
                    pixelFormat});
            }
        }

        void Swapchain::destroy()
        {
            if (!m_Backend)
                return;

            auto& backend = backendOf(m_Backend);

            backend.m_Buffers.clear();

            if (backend.m_Handle)
            {
                backend.m_Device.waitIdle();
                backend.m_Device.destroySwapchainKHR(backend.m_Handle, nullptr);
                backend.m_Handle = nullptr;
            }

            if (backend.m_Surface)
            {
                backend.m_Instance.destroySurfaceKHR(backend.m_Surface, nullptr);
                backend.m_Surface = nullptr;
            }

            backend.m_Window  = nullptr;
            backend.m_Surface = nullptr;

            backend.m_Instance       = nullptr;
            backend.m_PhysicalDevice = nullptr;
            backend.m_Device         = nullptr;

            backend.m_CurrentImageIndex = 0;
        }

        Rect2D getRenderArea(const Swapchain& swapchain)
        {
            return Rect2D {.offset = {0, 0}, .extent = {swapchain.getExtent()}};
        }
    } // namespace rhi
} // namespace vultra
