#include "vultra/function/rendering/backend/render_backend_system.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/engine/engine_context.hpp"
#include "vultra/core/rhi/structs/render_backend_api.hpp"
#include "vultra/core/rhi/vk/vulkan_imgui_backend.hpp"
#include "vultra/core/services/window_service.hpp"
#include "vultra/function/openxr/xr_headset.hpp"
#include "vultra/function/openxr/xr_helper.hpp"

#include <vbase/core/scoped_enum_flags.hpp>

#include <stdexcept>

namespace vultra
{
    namespace
    {
        void ensureXrMirrorTargets(rhi::RenderDevice&                                      rd,
                                   std::vector<rhi::Texture>&                              mirrorTargets,
                                   const std::span<const IRenderBackendService::XREyeView> eyeViews)
        {
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
                                      mirrorTarget.getPixelFormat() != eyeView.target->getPixelFormat();
                if (!recreate)
                    continue;

                mirrorTarget = rd.createTexture2D(eyeView.extent,
                                                  eyeView.target->getPixelFormat(),
                                                  1u,
                                                  0u,
                                                  rhi::ImageUsage::eTransferDst | rhi::ImageUsage::eSampled);
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
    } // namespace

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
        switch (ctx().config.render.backendApi)
        {
            case rhi::RenderBackendApi::eAuto:
            case rhi::RenderBackendApi::eVulkan:
                m_RenderDevice = std::make_unique<rhi::RenderDevice>(ctx().config.render.renderDeviceFeatureFlag,
                                                                      ctx().config.window.title,
                                                                      window.getRequiredVulkanInstanceExtensions());
                m_ImGuiBackend = std::make_unique<rhi::VulkanImGuiBackend>(*m_RenderDevice);
                break;

            case rhi::RenderBackendApi::eWebGPU:
                VULTRA_CORE_ERROR(
                    "[RenderBackendSystem] WebGPU backend is not implemented yet. TODO: wire in a WebGPU render "
                    "device backend (wgpu-native)");
                throw std::runtime_error("WebGPU backend is not implemented yet");
        }

        VULTRA_CORE_TRACE("[RenderBackendSystem] Creating swapchain");
        m_Swapchain =
            m_RenderDevice->createSwapchain(window, rhi::Swapchain::Format::esRGB, ctx().config.render.vSyncConfig);

        VULTRA_CORE_TRACE("[RenderBackendSystem] Creating frame controller");
        m_FrameController =
            std::make_unique<rhi::FrameController>(*m_RenderDevice, m_Swapchain, ctx().config.render.numFramesInFlight);

        if (HasFlagValues(ctx().config.render.renderDeviceFeatureFlag, rhi::RenderDeviceFeatureFlagBits::eOpenXR))
        {
            VULTRA_CORE_TRACE("[RenderBackendSystem] Creating XR render backend");
            if (!m_RenderDevice->getXRDevice())
            {
                VULTRA_CORE_WARN(
                    "[RenderBackendSystem] XR requested but unavailable; continuing in non-XR fallback mode");
            }
            else
            {
                m_XRBackend = std::make_unique<openxr::XRHeadset>(*m_RenderDevice);
            }
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
            m_RenderDevice->waitIdle();
        }

        m_ActiveCommandBuffer = nullptr;
        m_XREyeViews.clear();
        m_XRMirrorTargets.clear();
        m_XRFrameActive  = false;
        m_XRShouldRender = false;

        m_XRBackend.reset();
        m_ImGuiBackend.reset();
        m_FrameController.reset();
        m_Swapchain = {};
        m_RenderDevice.reset();
    }

    rhi::RenderDevice& RenderBackendSystem::renderDevice() { return *m_RenderDevice; }

    rhi::Swapchain& RenderBackendSystem::swapchain() { return m_Swapchain; }

    rhi::FrameController& RenderBackendSystem::frameController() { return *m_FrameController; }

    rhi::IImGuiBackend& RenderBackendSystem::imguiBackend() { return *m_ImGuiBackend; }

    bool RenderBackendSystem::beginFrame()
    {
        m_ActiveCommandBuffer = nullptr;
        m_XRFrameActive       = false;
        m_XRShouldRender      = false;
        m_XREyeViews.clear();

        if (m_XRBackend)
        {
            switch (m_XRBackend->beginFrame(m_XRSwapchainImageIndex))
            {
                case openxr::XRHeadset::BeginFrameResult::eError:
                    VULTRA_CORE_WARN("[RenderBackendSystem] XR beginFrame failed, skip frame");
                    return false;

                case openxr::XRHeadset::BeginFrameResult::eNormal:
                    m_XRFrameActive  = true;
                    m_XRShouldRender = true;

                    m_XREyeViews.reserve(m_XRBackend->getEyeCount());
                    for (uint32_t eyeIndex = 0; eyeIndex < static_cast<uint32_t>(m_XRBackend->getEyeCount());
                         ++eyeIndex)
                    {
                        auto& stereoTarget = m_XRBackend->getSwapchainStereoRenderTargetView(m_XRSwapchainImageIndex);
                        auto* eyeTarget    = (eyeIndex == 0u) ? &stereoTarget.left : &stereoTarget.right;

                        const auto eyeFov = m_XRBackend->getEyeFOV(eyeIndex);
                        const auto extent = m_XRBackend->getEyeResolution(eyeIndex);

                        m_XREyeViews.push_back({
                            .eyeIndex   = eyeIndex,
                            .view       = m_XRBackend->getEyeViewMatrix(eyeIndex),
                            .projection = xrutils::createProjectionMatrix(
                                {
                                    .angleLeft  = eyeFov.angleLeft,
                                    .angleRight = eyeFov.angleRight,
                                    .angleUp    = eyeFov.angleUp,
                                    .angleDown  = eyeFov.angleDown,
                                },
                                0.1f,
                                1000.0f),
                            .extent       = extent,
                            .target       = eyeTarget,
                            .stereoTarget = &stereoTarget.stereo,
                            .mirrorTarget = nullptr,
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

                case openxr::XRHeadset::BeginFrameResult::eSkipRender:
                    m_XRFrameActive  = true;
                    m_XRShouldRender = false;
                    VULTRA_CORE_TRACE("[RenderBackendSystem] XR frame requested skip-render");
                    break;

                case openxr::XRHeadset::BeginFrameResult::eSkipAll:
                    m_XRFrameActive  = false;
                    m_XRShouldRender = false;
                    return false;
            }
        }

        if (!m_FrameController->acquireNextFrame())
        {
            if (m_XRBackend && m_XRFrameActive)
            {
                m_XRBackend->endFrame();
                m_XRFrameActive  = false;
                m_XRShouldRender = false;
            }
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

    bool RenderBackendSystem::isXREnabled() const { return static_cast<bool>(m_XRBackend); }

    bool RenderBackendSystem::isXRMirrorEnabled() const { return m_XRMirrorEnabled; }

    bool RenderBackendSystem::isExitRequested() const { return m_XRBackend && m_XRBackend->isExitRequested(); }

    std::span<const IRenderBackendService::XREyeView> RenderBackendSystem::xrEyeViews() const { return m_XREyeViews; }

    void RenderBackendSystem::endFrame()
    {
        m_FrameController->endFrame();

        if (m_XRBackend && m_XRFrameActive)
        {
            m_XRBackend->endFrame();
        }

        m_ActiveCommandBuffer = nullptr;
        m_XRFrameActive       = false;
        m_XRShouldRender      = false;
        m_XREyeViews.clear();
    }

    void RenderBackendSystem::present() { m_FrameController->present(); }
} // namespace vultra

