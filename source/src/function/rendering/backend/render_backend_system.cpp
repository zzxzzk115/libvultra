#include "vultra/function/rendering/backend/render_backend_system.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/engine/engine_context.hpp"
#include "vultra/core/services/window_service.hpp"

namespace vultra
{
    bool RenderBackendSystem::onInit()
    {
        VULTRA_CORE_INFO("[RenderBackendSystem] Initializing...");

        VULTRA_CORE_TRACE("[RenderBackendSystem] Getting window service");
        auto& windowService = ctx().services.require<IWindowService>();
        auto& window        = windowService.window();

        VULTRA_CORE_TRACE("[RenderBackendSystem] Creating render device");
        m_RenderDevice =
            std::make_unique<rhi::RenderDevice>(ctx().config.render.renderDeviceFeatureFlag, ctx().config.window.title);

        VULTRA_CORE_TRACE("[RenderBackendSystem] Creating swapchain");
        m_Swapchain =
            m_RenderDevice->createSwapchain(window, rhi::Swapchain::Format::esRGB, ctx().config.render.vSyncConfig);

        VULTRA_CORE_TRACE("[RenderBackendSystem] Creating frame controller");
        m_FrameController =
            std::make_unique<rhi::FrameController>(*m_RenderDevice, m_Swapchain, ctx().config.render.numFramesInFlight);

        VULTRA_CORE_TRACE("[RenderBackendSystem] Providing IRenderBackendService");
        ctx().services.provide<IRenderBackendService>(this);

        VULTRA_CORE_INFO("[RenderBackendSystem] Initialized!");

        return true;
    }

    void RenderBackendSystem::onShutdown()
    {
        VULTRA_CORE_INFO("[RenderBackendSystem] Shutting down");
        m_RenderDevice->waitIdle();
    }

    rhi::RenderDevice& RenderBackendSystem::renderDevice() { return *m_RenderDevice; }

    rhi::Swapchain& RenderBackendSystem::swapchain() { return m_Swapchain; }

    rhi::FrameController& RenderBackendSystem::frameController() { return *m_FrameController; }

    bool RenderBackendSystem::beginFrame() { return m_FrameController->acquireNextFrame(); }

    rhi::CommandBuffer& RenderBackendSystem::commandBuffer() { return m_FrameController->beginFrame(); }

    rhi::Texture& RenderBackendSystem::backbuffer() { return m_Swapchain.getCurrentBuffer(); }

    void RenderBackendSystem::endFrame() { m_FrameController->endFrame(); }

    void RenderBackendSystem::present() { m_FrameController->present(); }
} // namespace vultra
