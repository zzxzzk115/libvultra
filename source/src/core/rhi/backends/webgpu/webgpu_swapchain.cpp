#include "vultra/core/rhi/backends/webgpu/webgpu_swapchain.hpp"

#include "vultra/core/os/window.hpp"
#include "vultra/core/rhi/backends/webgpu/conversions.hpp"
#include "vultra/core/rhi/interfaces/texture_access.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>

#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
#include <webgpu/webgpu.h>
#endif

namespace vultra
{
    namespace rhi
    {
        namespace
        {
            WGPUTexture g_CurrentWebGPUSwapchainTexture {nullptr};
        }

        WGPUTexture getCurrentWebGPUSwapchainTexture() { return g_CurrentWebGPUSwapchainTexture; }

#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
        namespace
        {
            class WebGPUSwapchain final : public ISwapchain
            {
            public:
                WebGPUSwapchain(const std::uintptr_t instance,
                                const std::uintptr_t physicalDevice,
                                const std::uintptr_t device,
                                os::Window*          window,
                                const SwapchainFormat format,
                                const VerticalSync   vsync) :
                    m_Instance(reinterpret_cast<WGPUInstance>(instance)),
                    m_Adapter(reinterpret_cast<WGPUAdapter>(physicalDevice)),
                    m_Device(reinterpret_cast<WGPUDevice>(device)),
                    m_Window(window), m_Format(format), m_Vsync(vsync)
                {
                    if (m_Instance == nullptr || m_Adapter == nullptr || m_Device == nullptr || m_Window == nullptr)
                    {
                        throw std::runtime_error("Invalid WebGPU swapchain creation arguments");
                    }

                    m_Surface = m_Window->createWebGPUSurface(m_Instance);
                    if (m_Surface == nullptr)
                    {
                        throw std::runtime_error("Failed to create WebGPU surface");
                    }

                    selectSurfaceConfig();
                    configureSurface();
                }

                ~WebGPUSwapchain() override { destroy(); }

                [[nodiscard]] bool isValid() const override { return m_Surface != nullptr; }

                [[nodiscard]] SwapchainFormat getFormat() const override { return m_Format; }

                [[nodiscard]] PixelFormat getPixelFormat() const override { return m_PixelFormat; }

                [[nodiscard]] Extent2D getExtent() const override { return m_Extent; }

                [[nodiscard]] std::size_t getNumBuffers() const override { return 1; }

                [[nodiscard]] std::uintptr_t getHandle() const override
                {
                    return reinterpret_cast<std::uintptr_t>(m_Surface);
                }

                [[nodiscard]] const std::vector<Texture>& getBuffers() const override { return m_Buffers; }

                [[nodiscard]] const Texture& getBuffer(uint32_t index) const override
                {
                    if (index >= m_Buffers.size())
                    {
                        return m_DummyTexture;
                    }
                    return m_Buffers[index];
                }

                [[nodiscard]] uint32_t getCurrentBufferIndex() const override { return 0; }

                [[nodiscard]] Texture& getCurrentBuffer() override
                {
                    if (m_Buffers.empty())
                    {
                        return m_DummyTexture;
                    }
                    return m_Buffers[0];
                }

                void recreate(const std::optional<VerticalSync> vsync) override
                {
                    if (vsync.has_value())
                    {
                        m_Vsync = *vsync;
                    }
                    selectSurfaceConfig();
                    configureSurface();
                }

                [[nodiscard]] bool acquireNextImage(std::uintptr_t) override
                {
                    if (m_Surface == nullptr)
                    {
                        if (!m_Buffers.empty())
                        {
                            m_Buffers[0] = {};
                        }
                        return false;
                    }

                    const auto fbExtent = m_Window->getFrameBufferExtent();
                    if (fbExtent.x <= 0 || fbExtent.y <= 0)
                    {
                        if (!m_Buffers.empty())
                        {
                            m_Buffers[0] = {};
                        }
                        releaseCurrentTexture();
                        return false;
                    }

                    const Extent2D desiredExtent {static_cast<uint32_t>(fbExtent.x), static_cast<uint32_t>(fbExtent.y)};
                    if (!m_Configured || desiredExtent.width != m_Extent.width || desiredExtent.height != m_Extent.height)
                    {
                        configureSurface();
                    }

                    releaseCurrentTexture();

                    wgpuSurfaceGetCurrentTexture(m_Surface, &m_CurrentSurfaceTexture);
                    switch (m_CurrentSurfaceTexture.status)
                    {
                        case WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal:
                        case WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal:
                            g_CurrentWebGPUSwapchainTexture = m_CurrentSurfaceTexture.texture;
                            if (!m_Buffers.empty() && m_CurrentSurfaceTexture.texture != nullptr)
                            {
                                m_Buffers[0] = TextureAccess::fromExternalImage(
                                    RenderBackendApi::eWebGPU,
                                    reinterpret_cast<std::uintptr_t>(m_Device),
                                    reinterpret_cast<std::uintptr_t>(m_CurrentSurfaceTexture.texture),
                                    m_Extent,
                                    m_PixelFormat);
                            }
                            return m_CurrentSurfaceTexture.texture != nullptr;
                        case WGPUSurfaceGetCurrentTextureStatus_Timeout:
                            if (!m_Buffers.empty())
                            {
                                m_Buffers[0] = {};
                            }
                            return false;
                        case WGPUSurfaceGetCurrentTextureStatus_Outdated:
                        case WGPUSurfaceGetCurrentTextureStatus_Lost:
                            configureSurface();
                            wgpuSurfaceGetCurrentTexture(m_Surface, &m_CurrentSurfaceTexture);
                            if (m_CurrentSurfaceTexture.status == WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal ||
                                m_CurrentSurfaceTexture.status == WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal)
                            {
                                g_CurrentWebGPUSwapchainTexture = m_CurrentSurfaceTexture.texture;
                                if (!m_Buffers.empty() && m_CurrentSurfaceTexture.texture != nullptr)
                                {
                                    m_Buffers[0] = TextureAccess::fromExternalImage(
                                        RenderBackendApi::eWebGPU,
                                        reinterpret_cast<std::uintptr_t>(m_Device),
                                        reinterpret_cast<std::uintptr_t>(m_CurrentSurfaceTexture.texture),
                                        m_Extent,
                                        m_PixelFormat);
                                }
                                return true;
                            }
                            if (!m_Buffers.empty())
                            {
                                m_Buffers[0] = {};
                            }
                            g_CurrentWebGPUSwapchainTexture = nullptr;
                            return false;
                        default:
                            if (!m_Buffers.empty())
                            {
                                m_Buffers[0] = {};
                            }
                            g_CurrentWebGPUSwapchainTexture = nullptr;
                            return false;
                    }
                }

            private:
                void selectSurfaceConfig()
                {
                    WGPUSurfaceCapabilities capabilities {};
                    if (wgpuSurfaceGetCapabilities(m_Surface, m_Adapter, &capabilities) != WGPUStatus_Success)
                    {
                        throw std::runtime_error("Failed to query WebGPU surface capabilities");
                    }

                    const auto preferredFormat = webgpu::preferredSwapchainFormat(m_Format);
                    m_SurfaceFormat            = capabilities.formatCount > 0 ? capabilities.formats[0] : preferredFormat;
                    for (size_t i = 0; i < capabilities.formatCount; ++i)
                    {
                        if (capabilities.formats[i] == preferredFormat)
                        {
                            m_SurfaceFormat = preferredFormat;
                            break;
                        }
                        if (webgpu::matchesSwapchainFormat(capabilities.formats[i], m_Format))
                        {
                            m_SurfaceFormat = capabilities.formats[i];
                        }
                    }

                    const auto preferredPresentMode = webgpu::toWgpuPresentMode(m_Vsync);
                    m_PresentMode = capabilities.presentModeCount > 0 ? capabilities.presentModes[0] : WGPUPresentMode_Fifo;
                    for (size_t i = 0; i < capabilities.presentModeCount; ++i)
                    {
                        if (capabilities.presentModes[i] == preferredPresentMode)
                        {
                            m_PresentMode = preferredPresentMode;
                            break;
                        }
                    }

                    m_AlphaMode = capabilities.alphaModeCount > 0 ? capabilities.alphaModes[0] : WGPUCompositeAlphaMode_Auto;
                    m_PixelFormat = webgpu::toPixelFormat(m_SurfaceFormat);
                    wgpuSurfaceCapabilitiesFreeMembers(capabilities);
                }

                void configureSurface()
                {
                    if (m_Surface == nullptr)
                    {
                        return;
                    }

                    releaseCurrentTexture();

                    const auto fbExtent = m_Window->getFrameBufferExtent();
                    m_Extent.width      = static_cast<uint32_t>(std::max(1, fbExtent.x));
                    m_Extent.height     = static_cast<uint32_t>(std::max(1, fbExtent.y));

                    WGPUSurfaceConfiguration config {};
                    config.device          = m_Device;
                    config.format          = m_SurfaceFormat;
                    config.usage           = WGPUTextureUsage_RenderAttachment;
                    config.width           = m_Extent.width;
                    config.height          = m_Extent.height;
                    config.viewFormatCount = 0;
                    config.viewFormats     = nullptr;
                    config.alphaMode       = m_AlphaMode;
                    config.presentMode     = m_PresentMode;
                    wgpuSurfaceConfigure(m_Surface, &config);
                    m_Configured = true;
                    if (m_Buffers.empty())
                    {
                        m_Buffers.emplace_back();
                    }
                    else
                    {
                        m_Buffers[0] = {};
                    }
                }

                void releaseCurrentTexture()
                {
                    if (m_CurrentSurfaceTexture.texture)
                    {
                        if (!m_Buffers.empty())
                        {
                            // Release dependent views before releasing the acquired swapchain image.
                            m_Buffers[0] = {};
                        }
                        wgpuTextureRelease(m_CurrentSurfaceTexture.texture);
                        m_CurrentSurfaceTexture.texture = nullptr;
                    }
                    m_CurrentSurfaceTexture.status = WGPUSurfaceGetCurrentTextureStatus_Error;
                    g_CurrentWebGPUSwapchainTexture = nullptr;
                }

                void destroy()
                {
                    releaseCurrentTexture();
                    if (m_Surface)
                    {
                        if (m_Configured)
                        {
                            wgpuSurfaceUnconfigure(m_Surface);
                        }
                        wgpuSurfaceRelease(m_Surface);
                        m_Surface = nullptr;
                    }
                    m_Configured = false;
                }

            private:
                WGPUInstance       m_Instance {nullptr};
                WGPUAdapter        m_Adapter {nullptr};
                WGPUDevice         m_Device {nullptr};
                os::Window*        m_Window {nullptr};
                WGPUSurface        m_Surface {nullptr};
                SwapchainFormat    m_Format {SwapchainFormat::eLinear};
                VerticalSync       m_Vsync {VerticalSync::eDisabled};
                PixelFormat        m_PixelFormat {PixelFormat::eUndefined};
                Extent2D           m_Extent {};
                WGPUTextureFormat  m_SurfaceFormat {WGPUTextureFormat_Undefined};
                WGPUPresentMode    m_PresentMode {WGPUPresentMode_Fifo};
                WGPUCompositeAlphaMode m_AlphaMode {WGPUCompositeAlphaMode_Auto};
                WGPUSurfaceTexture m_CurrentSurfaceTexture {};
                bool               m_Configured {false};
                std::vector<Texture> m_DummyBuffers;
                Texture              m_DummyTexture;
                std::vector<Texture> m_Buffers;
            };
        } // namespace
#endif

        std::shared_ptr<ISwapchain> createWebGPUSwapchain(const std::uintptr_t instance,
                                                                  const std::uintptr_t physicalDevice,
                                                                  const std::uintptr_t device,
                                                                  os::Window*          window,
                                                                  const SwapchainFormat format,
                                                                  const VerticalSync   vsync)
        {
#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
            return std::make_shared<WebGPUSwapchain>(instance, physicalDevice, device, window, format, vsync);
#else
            (void)instance;
            (void)physicalDevice;
            (void)device;
            (void)window;
            (void)format;
            (void)vsync;
            throw std::runtime_error("WebGPU swapchain backend is disabled for this build");
#endif
        }
    } // namespace rhi
} // namespace vultra
