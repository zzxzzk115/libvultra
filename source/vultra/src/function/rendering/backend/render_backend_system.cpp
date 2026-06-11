#include "vultra/function/rendering/backend/render_backend_system.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/engine/engine_context.hpp"
#include "vultra/core/rhi/structs/render_backend_api.hpp"
#include "vultra/function/plugin/plugin_system.hpp"
#if defined(VULTRA_ENABLE_VULKAN) && VULTRA_ENABLE_VULKAN
#include "vultra/core/rhi/backends/vk/vulkan_render_device_access.hpp"
#include "vultra/core/rhi/backends/vk/vulkan_imgui.hpp"
#endif
#include "vultra/core/rhi/backends/webgpu/webgpu_imgui.hpp"
#include "vultra/core/services/window_service.hpp"
#include "vultra/function/services/render_backend_extension_service.hpp"
#if defined(VULTRA_ENABLE_XR) && VULTRA_ENABLE_XR
#include "vultra/function/openxr/xr_headset.hpp"
#include "vultra/function/openxr/xr_helper.hpp"
#endif

#include <vbase/core/scoped_enum_flags.hpp>

#include <stdexcept>

namespace vultra
{
    namespace
    {
        [[nodiscard]] std::unique_ptr<rhi::RenderDevice>
        createRenderDevice(rhi::RenderDeviceFeatureFlagBits featureFlags,
                           std::string_view                 title,
                           std::span<const char* const>     vulkanInstanceExtensions,
                           std::span<const char* const>     vulkanDeviceExtensions,
                           rhi::RenderBackendApi            backendApi,
                           bool                             enableValidation,
                           bool                             enableDebugMarkers,
                           bool                             enableRenderDoc,
                           rhi::VulkanHookTable             vulkanHooks)
        {
            return std::make_unique<rhi::RenderDevice>(featureFlags,
                                                       title,
                                                       vulkanInstanceExtensions,
                                                       vulkanDeviceExtensions,
                                                       backendApi,
                                                       enableValidation,
                                                       enableDebugMarkers,
                                                       enableRenderDoc,
                                                       vulkanHooks);
        }

#if defined(VULTRA_ENABLE_XR) && VULTRA_ENABLE_XR
        void ensureXrMirrorTargets(rhi::RenderDevice&                                      rd,
                                   std::vector<rhi::Texture>&                              mirrorTargets,
                                   const std::span<const IRenderBackendService::XREyeView> eyeViews)
        {
            constexpr rhi::PixelFormat kMirrorPreviewFormat = rhi::PixelFormat::eRGBA8_sRGB;
            constexpr rhi::ImageUsage  kMirrorPreviewUsage  = rhi::ImageUsage::eTransferDst | rhi::ImageUsage::eSampled;

            if (eyeViews.empty())
            {
                mirrorTargets.clear();
                return;
            }

            if (mirrorTargets.size() != eyeViews.size())
                mirrorTargets.resize(eyeViews.size());

            for (size_t eyeIndex = 0; eyeIndex < eyeViews.size(); ++eyeIndex)
            {
                const auto& eyeView = eyeViews[eyeIndex];
                if (!eyeView.target)
                    continue;

                auto&      mirrorTarget = mirrorTargets[eyeIndex];
                const bool recreate     = !mirrorTarget || mirrorTarget.getExtent().width != eyeView.extent.width ||
                                      mirrorTarget.getExtent().height != eyeView.extent.height ||
                                      mirrorTarget.getPixelFormat() != kMirrorPreviewFormat;
                if (!recreate)
                    continue;

                mirrorTarget = rd.createTexture2D(eyeView.extent, kMirrorPreviewFormat, 1u, 0u, kMirrorPreviewUsage);
                rd.setupSampler(mirrorTarget,
                                rhi::SamplerInfo {
                                    .magFilter    = rhi::TexelFilter::eLinear,
                                    .minFilter    = rhi::TexelFilter::eLinear,
                                    .addressModeS = rhi::SamplerAddressMode::eClampToEdge,
                                    .addressModeT = rhi::SamplerAddressMode::eClampToEdge,
                                    .addressModeR = rhi::SamplerAddressMode::eClampToEdge,
                                });
            }
        }
#endif
    } // namespace

    RenderBackendSystem::RenderBackendSystem()  = default;
    RenderBackendSystem::~RenderBackendSystem() = default;

    bool RenderBackendSystem::onInit()
    {
        VULTRA_CORE_INFO("[RenderBackendSystem] Initializing...");

        VULTRA_CORE_TRACE("[RenderBackendSystem] Getting window service");
        auto& windowService = ctx().services.require<IWindowService>();
        auto& window        = windowService.window();

        if (!window.isReady())
        {
            VULTRA_CORE_INFO("[RenderBackendSystem] Waiting for window to become ready...");
            while (!window.isReady() && !window.shouldClose())
            {
                window.pollEvents(-1);
            }
            VULTRA_CORE_INFO("[RenderBackendSystem] Window ready state: {}", window.isReady());
        }

        VULTRA_CORE_TRACE("[RenderBackendSystem] Creating render device");
        if (!loadPreRenderDeviceNativePlugins(ctx()))
            VULTRA_CORE_ERROR("[RenderBackendSystem] One or more pre-render-device native plugins failed to load.");

        auto* backendExtensionService = ctx().services.tryGet<IRenderBackendExtensionService>();
        rhi::VulkanHookTable vulkanHooks {};
        VulkanDeviceRequirements vulkanDeviceRequirements {};
        if (auto* extension = backendExtensionService != nullptr ? backendExtensionService->extension() : nullptr)
        {
            vulkanHooks = backendExtensionService->vulkanHooks();
            if (!vulkanHooks.empty())
            {
                VULTRA_CORE_INFO("[RenderBackendSystem] Backend extension '{}' provided Vulkan hook table.",
                                 extension->name());
            }
            extension->beforeVulkanInstanceCreate();
            extension->collectVulkanDeviceRequirements(vulkanDeviceRequirements);
            if (!vulkanDeviceRequirements.deviceExtensions.empty())
            {
                VULTRA_CORE_INFO("[RenderBackendSystem] Backend extension '{}' requested {} Vulkan device extension(s).",
                                 extension->name(),
                                 vulkanDeviceRequirements.deviceExtensions.size());
            }
            extension->beforeVulkanDeviceCreate();
        }
        std::vector<const char*> vulkanDeviceExtensionNames;
        vulkanDeviceExtensionNames.reserve(vulkanDeviceRequirements.deviceExtensions.size());
        for (const auto& extension : vulkanDeviceRequirements.deviceExtensions)
            vulkanDeviceExtensionNames.push_back(extension.c_str());
        auto requestedBackendApi = ctx().config.render.backendApi;
#if defined(__ANDROID__)
        if (requestedBackendApi == rhi::RenderBackendApi::eWebGPU)
        {
            VULTRA_CORE_WARN("[RenderBackendSystem] WebGPU is disabled on Android. Falling back to Vulkan backend.");
            requestedBackendApi = rhi::RenderBackendApi::eVulkan;
        }
#endif

        switch (requestedBackendApi)
        {
#if defined(VULTRA_ENABLE_VULKAN) && VULTRA_ENABLE_VULKAN
            case rhi::RenderBackendApi::eAuto:
            case rhi::RenderBackendApi::eVulkan: {
                auto featureFlags = ctx().config.render.renderDeviceFeatureFlag;
                try
                {
                    m_RenderDevice = createRenderDevice(featureFlags,
                                                        ctx().config.window.title,
                                                        window.getRequiredVulkanInstanceExtensions(),
                                                        vulkanDeviceExtensionNames,
                                                        rhi::RenderBackendApi::eVulkan,
                                                        ctx().config.render.enableValidation,
                                                        ctx().config.render.enableDebugMarkers,
                                                        ctx().config.render.enableRenderDoc,
                                                        vulkanHooks);
                }
                catch (const std::runtime_error& e)
                {
                    if (!HasFlagValues(featureFlags, rhi::RenderDeviceFeatureFlagBits::eRayTracingPipeline))
                        throw;

                    VULTRA_CORE_WARN("[RenderBackendSystem] Ray tracing was requested but is unavailable ({}); falling "
                                     "back to normal Vulkan rendering.",
                                     e.what());
                    featureFlags                                = rhi::RenderDeviceFeatureFlagBits::eNormal;
                    ctx().config.render.renderDeviceFeatureFlag = featureFlags;
                    m_RenderDevice                              = createRenderDevice(featureFlags,
                                                        ctx().config.window.title,
                                                        window.getRequiredVulkanInstanceExtensions(),
                                                        vulkanDeviceExtensionNames,
                                                        rhi::RenderBackendApi::eVulkan,
                                                        ctx().config.render.enableValidation,
                                                        ctx().config.render.enableDebugMarkers,
                                                        ctx().config.render.enableRenderDoc,
                                                        vulkanHooks);
                }
                m_ImGuiBackend = std::make_unique<rhi::VulkanImGui>(*m_RenderDevice);
                break;
            }
#else
            case rhi::RenderBackendApi::eAuto:
            case rhi::RenderBackendApi::eVulkan:
                VULTRA_CORE_WARN("[RenderBackendSystem] Vulkan backend is disabled in this build; using WebGPU.");
                [[fallthrough]];
#endif

            case rhi::RenderBackendApi::eWebGPU:
                m_RenderDevice = createRenderDevice(ctx().config.render.renderDeviceFeatureFlag,
                                                    ctx().config.window.title,
                                                    std::span<const char* const> {},
                                                    std::span<const char* const> {},
                                                    rhi::RenderBackendApi::eWebGPU,
                                                    ctx().config.render.enableValidation,
                                                    ctx().config.render.enableDebugMarkers,
                                                    ctx().config.render.enableRenderDoc,
                                                    vulkanHooks);
                m_ImGuiBackend = std::make_unique<rhi::WebGPUImGui>(*m_RenderDevice);
                break;
        }

        if (!m_RenderDevice->supportsSwapchain())
        {
            throw std::runtime_error(
                std::format("Backend '{}' does not support swapchain yet", m_RenderDevice->getName()));
        }
        if (auto* extension = backendExtensionService != nullptr ? backendExtensionService->extension() : nullptr)
        {
            extension->afterVulkanDeviceCreate(*m_RenderDevice);
#if defined(VULTRA_ENABLE_VULKAN) && VULTRA_ENABLE_VULKAN
            const auto queueFamily = rhi::VulkanRenderDeviceAccess::getQueueFamilyIndex(*m_RenderDevice);
            extension->afterVulkanNativeDeviceCreate(VulkanNativeDevice {
                .instance            = rhi::VulkanRenderDeviceAccess::getInstanceHandle(*m_RenderDevice),
                .physicalDevice      = rhi::VulkanRenderDeviceAccess::getPhysicalDeviceHandle(*m_RenderDevice),
                .device              = rhi::VulkanRenderDeviceAccess::getDeviceHandle(*m_RenderDevice),
                .graphicsQueue       = rhi::VulkanRenderDeviceAccess::getQueueHandle(*m_RenderDevice),
                .graphicsQueueFamily = queueFamily >= 0 ? static_cast<uint32_t>(queueFamily) : 0u,
                .graphicsQueueIndex  = 0u,
            });
#else
            extension->afterVulkanNativeDeviceCreate(VulkanNativeDevice {});
#endif
            extension->beforeSwapchainCreate();
        }

        VULTRA_CORE_TRACE("[RenderBackendSystem] Creating swapchain");
        m_Swapchain = m_RenderDevice->createSwapchain(
            window, ctx().config.render.swapchainFormat, ctx().config.render.vSyncConfig);
        if (auto* extension = backendExtensionService != nullptr ? backendExtensionService->extension() : nullptr)
            extension->afterSwapchainCreate(m_Swapchain);

        VULTRA_CORE_TRACE("[RenderBackendSystem] Creating frame controller");
        m_FrameController =
            std::make_unique<rhi::FrameController>(*m_RenderDevice, m_Swapchain, ctx().config.render.numFramesInFlight);

        if (HasFlagValues(ctx().config.render.renderDeviceFeatureFlag, rhi::RenderDeviceFeatureFlagBits::eXR))
        {
#if defined(VULTRA_ENABLE_XR) && VULTRA_ENABLE_XR
            if (!m_RenderDevice->getXRDevice())
            {
                VULTRA_CORE_WARN(
                    "[RenderBackendSystem] XR-capable Vulkan device was requested but OpenXR is unavailable.");
            }
#else
            VULTRA_CORE_WARN("[RenderBackendSystem] XR is disabled in this build; ignoring XR feature flag.");
#endif
        }

        bool xrMirrorEnabled = ctx().config.render.xr.mirror;
        VULTRA_CORE_TRACE("[RenderBackendSystem] XR Mirror Mode: {}", xrMirrorEnabled ? "Enabled" : "Disabled");
        m_XRMirrorEnabled = xrMirrorEnabled;

        VULTRA_CORE_TRACE("[RenderBackendSystem] Providing IRenderBackendService");
        ctx().services.provide<IRenderBackendService>(this);

        VULTRA_CORE_INFO("[RenderBackendSystem] Initialized!");

        return true;
    }

    void RenderBackendSystem::onShutdown()
    {
        VULTRA_CORE_INFO("[RenderBackendSystem] Shutting down");

        if (m_RenderDevice)
        {
            if (auto* extensionService = ctx().services.tryGet<IRenderBackendExtensionService>();
                extensionService != nullptr)
            {
                if (auto* extension = extensionService->extension())
                    extension->beforeDeviceWaitIdle();
            }
            m_RenderDevice->waitIdle();
            if (auto* extensionService = ctx().services.tryGet<IRenderBackendExtensionService>();
                extensionService != nullptr)
            {
                if (auto* extension = extensionService->extension())
                    extension->afterDeviceWaitIdle();
            }
        }

        m_ActiveCommandBuffer = nullptr;
        m_XREyeViews.clear();
        m_LastXREyeViews.clear();
        m_XRMirrorTargets.clear();
        m_XRFrameActive      = false;
        m_XRShouldRender     = false;
        m_XRSessionRequested = false;
        m_XRSessionUserClosed = false;
        m_XRSessionRestartPending = false;
        m_XRSessionStartDeferred  = false;

#if defined(VULTRA_ENABLE_XR) && VULTRA_ENABLE_XR
        m_XRBackend.reset();
#endif
        m_ImGuiBackend.reset();
        m_FrameController.reset();
        if (auto* extensionService = ctx().services.tryGet<IRenderBackendExtensionService>();
            extensionService != nullptr)
        {
            if (auto* extension = extensionService->extension())
                extension->beforeSwapchainDestroy();
        }
        m_Swapchain = {};
        if (auto* extensionService = ctx().services.tryGet<IRenderBackendExtensionService>();
            extensionService != nullptr)
        {
            if (auto* extension = extensionService->extension())
                extension->afterSwapchainDestroy();
        }
        m_RenderDevice.reset();
    }

    rhi::RenderDevice& RenderBackendSystem::renderDevice() { return *m_RenderDevice; }

    rhi::Swapchain& RenderBackendSystem::swapchain() { return m_Swapchain; }

    rhi::FrameController& RenderBackendSystem::frameController() { return *m_FrameController; }

    rhi::IImGui& RenderBackendSystem::imguiBackend() { return *m_ImGuiBackend; }

    bool RenderBackendSystem::beginFrame()
    {
        m_ActiveCommandBuffer = nullptr;
        m_XRFrameActive       = false;
        m_XRShouldRender      = false;
        m_XREyeViews.clear();

#if defined(VULTRA_ENABLE_XR) && VULTRA_ENABLE_XR
        if (m_XRSessionRestartPending && m_XRBackend)
        {
            m_RenderDevice->waitIdle();
            m_XRBackend.reset();
            m_XRMirrorTargets.clear();
            m_XREyeViews.clear();
            m_LastXREyeViews.clear();
            m_XRFrameActive             = false;
            m_XRShouldRender            = false;
            m_XRSessionRestartPending   = false;
            m_XRSessionStartDeferred    = true;
            VULTRA_CORE_INFO("[RenderBackendSystem] XR session restarted; released old session, skipping transition frame");
            return false;
        }
        if (m_XRSessionStartDeferred)
        {
            m_XRSessionStartDeferred = false;
            VULTRA_CORE_INFO("[RenderBackendSystem] XR session restart deferred one frame before start");
            return false;
        }
        if (!m_XRSessionRequested && m_XRBackend)
        {
            m_RenderDevice->waitIdle();
            m_XRBackend.reset();
            m_XRMirrorTargets.clear();
            m_XREyeViews.clear();
            m_LastXREyeViews.clear();
            m_XRSessionRestartPending = false;
            m_XRSessionStartDeferred  = false;
            VULTRA_CORE_INFO("[RenderBackendSystem] XR session released; skipping transition frame");
            return false;
        }
        if (m_XRSessionRequested && !m_XRSessionUserClosed && !m_XRBackend && m_RenderDevice->getXRDevice())
        {
            try
            {
                m_RenderDevice->waitIdle();
                VULTRA_CORE_INFO("[RenderBackendSystem] Starting XR session on demand");
                m_XRBackend = std::make_unique<openxr::XRHeadset>(*m_RenderDevice);
                m_XRMirrorTargets.clear();
                m_XREyeViews.clear();
                m_LastXREyeViews.clear();
                VULTRA_CORE_INFO("[RenderBackendSystem] XR session started; skipping transition frame");
                return false;
            }
            catch (const std::exception& e)
            {
                VULTRA_CORE_WARN("[RenderBackendSystem] Failed to start XR session: {}", e.what());
                m_XRSessionRequested = false;
            }
        }
        if (m_XRBackend)
        {
            switch (m_XRBackend->beginFrame(m_XRSwapchainImageIndex))
            {
                case openxr::XRHeadset::BeginFrameResult::eError:
                    VULTRA_CORE_WARN("[RenderBackendSystem] XR beginFrame failed, skip frame");
                    return false;

                case openxr::XRHeadset::BeginFrameResult::eNormal: {
                    m_XRFrameActive  = true;
                    m_XRShouldRender = true;

                    m_XREyeViews.reserve(m_XRBackend->getEyeCount());
                    const auto viewStateFlags       = m_XRBackend->getViewStateFlags();
                    const bool positionValid        = (viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) != 0;
                    const bool orientationValid     = (viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) != 0;
                    const bool positionTracked      = (viewStateFlags & XR_VIEW_STATE_POSITION_TRACKED_BIT) != 0;
                    const bool orientationTracked   = (viewStateFlags & XR_VIEW_STATE_ORIENTATION_TRACKED_BIT) != 0;
                    const auto headPosition         = m_XRBackend->getHeadPosition();
                    const auto headRotation         = m_XRBackend->getHeadRotation();
                    const auto ipd                  = m_XRBackend->getIPD();
                    const auto predictedDisplayTime = m_XRBackend->getPredictedDisplayTime();
                    for (uint32_t eyeIndex = 0; eyeIndex < static_cast<uint32_t>(m_XRBackend->getEyeCount());
                         ++eyeIndex)
                    {
                        auto& stereoTarget = m_XRBackend->getSwapchainStereoRenderTargetView(m_XRSwapchainImageIndex);
                        auto* eyeTarget    = (eyeIndex == 0u) ? &stereoTarget.left : &stereoTarget.right;

                        const auto extent = m_XRBackend->getEyeResolution(eyeIndex);
                        const auto fov    = m_XRBackend->getEyeFOV(eyeIndex);

                        m_XREyeViews.push_back({
                            .eyeIndex     = eyeIndex,
                            .view         = m_XRBackend->getEyeViewMatrix(eyeIndex),
                            .projection   = m_XRBackend->getEyeProjectionMatrix(eyeIndex),
                            .pose         = m_XRBackend->getEyePoseMatrix(eyeIndex),
                            .fov          = glm::vec4(fov.angleLeft, fov.angleRight, fov.angleUp, fov.angleDown),
                            .headPosition = headPosition,
                            .headRotation = headRotation,
                            .eyePosition  = m_XRBackend->getEyePosition(eyeIndex),
                            .eyeRotation  = m_XRBackend->getEyeRotation(eyeIndex),
                            .ipd          = ipd,
                            .predictedDisplayTime = predictedDisplayTime,
                            .positionValid        = positionValid,
                            .orientationValid     = orientationValid,
                            .positionTracked      = positionTracked,
                            .orientationTracked   = orientationTracked,
                            .extent               = extent,
                            .target               = eyeTarget,
                            .stereoTarget         = &stereoTarget.stereo,
                            .mirrorTarget         = nullptr,
                        });
                    }

                    if (m_XRMirrorEnabled)
                    {
                        ensureXrMirrorTargets(*m_RenderDevice, m_XRMirrorTargets, m_XREyeViews);
                        for (size_t eyeIndex = 0; eyeIndex < m_XREyeViews.size() && eyeIndex < m_XRMirrorTargets.size();
                             ++eyeIndex)
                        {
                            m_XREyeViews[eyeIndex].mirrorTarget =
                                m_XRMirrorTargets[eyeIndex] ? &m_XRMirrorTargets[eyeIndex] : nullptr;
                        }
                    }
                    break;
                }

                case openxr::XRHeadset::BeginFrameResult::eSkipRender:
                    m_XRFrameActive  = true;
                    m_XRShouldRender = false;
                    VULTRA_CORE_TRACE("[RenderBackendSystem] XR frame requested skip-render");
                    break;

                case openxr::XRHeadset::BeginFrameResult::eSkipAll:
                    if (m_XRBackend && m_XRBackend->isSessionCloseRequested())
                    {
                        m_RenderDevice->waitIdle();
                        m_XRBackend.reset();
                        m_XRMirrorTargets.clear();
                        m_XREyeViews.clear();
                        m_LastXREyeViews.clear();
                        m_XRFrameActive       = false;
                        m_XRShouldRender      = false;
                        m_XRSessionRequested  = false;
                        m_XRSessionRestartPending = false;
                        m_XRSessionStartDeferred  = false;
                        m_XRSessionUserClosed = true;
                        VULTRA_CORE_INFO("[RenderBackendSystem] XR session closed by runtime/user");
                    }
                    m_XRFrameActive  = false;
                    m_XRShouldRender = false;
                    return false;
            }
        }
#endif

        if (auto* extensionService = ctx().services.tryGet<IRenderBackendExtensionService>();
            extensionService != nullptr)
        {
            if (auto* extension = extensionService->extension())
                extension->beforeAcquireNextImage();
        }
        const bool acquired = m_FrameController->acquireNextFrame();
        if (auto* extensionService = ctx().services.tryGet<IRenderBackendExtensionService>();
            extensionService != nullptr)
        {
            if (auto* extension = extensionService->extension())
                extension->afterAcquireNextImage(acquired);
        }
        if (!acquired)
        {
#if defined(VULTRA_ENABLE_XR) && VULTRA_ENABLE_XR
            if (m_XRBackend && m_XRFrameActive)
            {
                m_XRBackend->endFrame();
                m_XRFrameActive  = false;
                m_XRShouldRender = false;
            }
#endif
            return false;
        }

        return true;
    }

    rhi::CommandBuffer& RenderBackendSystem::commandBuffer()
    {
        if (!m_ActiveCommandBuffer)
        {
            m_ActiveCommandBuffer = &m_FrameController->beginFrame();
        }
        return *m_ActiveCommandBuffer;
    }

    rhi::Texture& RenderBackendSystem::backbuffer() { return m_Swapchain.getCurrentBuffer(); }

    bool RenderBackendSystem::isXREnabled() const
    {
#if defined(VULTRA_ENABLE_XR) && VULTRA_ENABLE_XR
        return static_cast<bool>(m_XRBackend);
#else
        return false;
#endif
    }

    bool RenderBackendSystem::isXRMirrorEnabled() const { return m_XRMirrorEnabled; }

    void RenderBackendSystem::requestXRSession(const bool requested)
    {
        if (!requested)
        {
            m_XRSessionUserClosed = false;
#if defined(VULTRA_ENABLE_XR) && VULTRA_ENABLE_XR
            if (m_XRBackend)
                m_XRSessionRestartPending = true;
            else
                m_XRSessionStartDeferred = false;
#else
            m_XRSessionStartDeferred = false;
#endif
        }
        else if (m_XRSessionRestartPending)
        {
            m_XRSessionUserClosed  = false;
            m_XRSessionRequested   = true;
            return;
        }
        m_XRSessionRequested = requested && !m_XRSessionUserClosed;
    }

    bool RenderBackendSystem::isExitRequested() const
    {
#if defined(VULTRA_ENABLE_XR) && VULTRA_ENABLE_XR
        return m_XRBackend && m_XRBackend->isExitRequested();
#else
        return false;
#endif
    }

    std::span<const IRenderBackendService::XREyeView> RenderBackendSystem::xrEyeViews() const { return m_XREyeViews; }

    std::span<const IRenderBackendService::XREyeView> RenderBackendSystem::lastXREyeViews() const
    {
        return m_LastXREyeViews;
    }

    void RenderBackendSystem::endFrame()
    {
        m_FrameController->endFrame();

#if defined(VULTRA_ENABLE_XR) && VULTRA_ENABLE_XR
        if (m_XRBackend && m_XRFrameActive)
        {
            m_XRBackend->endFrame();
        }
#endif

        m_ActiveCommandBuffer = nullptr;
        m_XRFrameActive       = false;
        m_XRShouldRender      = false;
        m_LastXREyeViews      = m_XREyeViews;
        m_XREyeViews.clear();
    }

    void RenderBackendSystem::present()
    {
        if (auto* extensionService = ctx().services.tryGet<IRenderBackendExtensionService>();
            extensionService != nullptr)
        {
            if (auto* extension = extensionService->extension())
                extension->beforePresent();
        }
        m_FrameController->present();
        if (auto* extensionService = ctx().services.tryGet<IRenderBackendExtensionService>();
            extensionService != nullptr)
        {
            if (auto* extension = extensionService->extension())
                extension->afterPresent();
        }
    }
} // namespace vultra
