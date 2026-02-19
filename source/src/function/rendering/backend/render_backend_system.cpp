#include "vultra/function/rendering/backend/render_backend_system.hpp"
#include "vultra/core/engine/engine_context.hpp"
#include "vultra/core/services/window_service.hpp"

namespace vultra
{

    bool RenderBackendSystem::onInit()
    {
        auto& windowService = ctx().services.require<IWindowService>();

        auto& window = windowService.window();

        m_RenderDevice = std::make_unique<rhi::RenderDevice>(ctx().config.renderDeviceFeatureFlag, ctx().config.title);

        m_Swapchain = m_RenderDevice->createSwapchain(window, rhi::Swapchain::Format::esRGB, ctx().config.vSyncConfig);

        m_FrameController =
            std::make_unique<rhi::FrameController>(*m_RenderDevice, m_Swapchain, ctx().config.numFramesInFlight);

        ctx().services.provide<IRenderBackendService>(this);

        return true;
    }

    void RenderBackendSystem::onShutdown() { m_RenderDevice->waitIdle(); }

    rhi::RenderDevice& RenderBackendSystem::renderDevice() { return *m_RenderDevice; }

    rhi::Swapchain& RenderBackendSystem::swapchain() { return m_Swapchain; }

    rhi::FrameController& RenderBackendSystem::frameController() { return *m_FrameController; }

    bool RenderBackendSystem::beginFrame() { return m_FrameController->acquireNextFrame(); }

    rhi::CommandBuffer& RenderBackendSystem::commandBuffer() { return m_FrameController->beginFrame(); }

    rhi::Texture& RenderBackendSystem::backbuffer() { return m_Swapchain.getCurrentBuffer(); }

    void RenderBackendSystem::endFrame() { m_FrameController->endFrame(); }

    void RenderBackendSystem::present() { m_FrameController->present(); }
} // namespace vultra
