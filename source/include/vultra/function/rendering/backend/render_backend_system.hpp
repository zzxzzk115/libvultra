#pragma once

#include "vultra/core/engine/engine_subsystem.hpp"
#include "vultra/function/services/render_backend_service.hpp"

#include <memory>

namespace vultra
{
    class RenderBackendSystem final : public EngineSubsystem, public IRenderBackendService
    {
    public:
        ENGINE_SUBSYSTEM(RenderBackendSystem)

        bool onInit() override;
        void onShutdown() override;

    public:
        rhi::RenderDevice&    renderDevice() override;
        rhi::Swapchain&       swapchain() override;
        rhi::FrameController& frameController() override;

        bool                beginFrame() override;
        rhi::CommandBuffer& commandBuffer() override;
        rhi::Texture&       backbuffer() override;
        void                endFrame() override;
        void                present() override;

    private:
        std::unique_ptr<rhi::RenderDevice>    m_RenderDevice;
        std::unique_ptr<rhi::FrameController> m_FrameController;

        rhi::Swapchain m_Swapchain;
    };
} // namespace vultra
