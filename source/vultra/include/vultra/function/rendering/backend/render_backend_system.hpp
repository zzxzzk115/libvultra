#pragma once

#include "vultra/core/engine/engine_subsystem.hpp"
#include "vultra/core/rhi/interfaces/iimgui.hpp"
#include "vultra/function/services/render_backend_service.hpp"

#include <memory>
#include <vector>

namespace vultra
{
#if defined(VULTRA_ENABLE_XR) && VULTRA_ENABLE_XR
    namespace openxr
    {
        class XRHeadset;
    }
#endif

    class RenderBackendSystem final : public EngineSubsystem, public IRenderBackendService
    {
    public:
        ENGINE_SUBSYSTEM(RenderBackendSystem)
        RenderBackendSystem();
        ~RenderBackendSystem() override;

        bool onInit() override;
        void onShutdown() override;

    public:
        rhi::RenderDevice&    renderDevice() override;
        rhi::Swapchain&       swapchain() override;
        rhi::FrameController& frameController() override;
        rhi::IImGui&   imguiBackend() override;

        bool                beginFrame() override;
        rhi::CommandBuffer& commandBuffer() override;
        rhi::Texture&       backbuffer() override;

        bool                       isXREnabled() const override;
        bool                       isXRMirrorEnabled() const override;
        bool                       isExitRequested() const override;
        std::span<const XREyeView> xrEyeViews() const override;

        void endFrame() override;
        void present() override;

    private:
        std::unique_ptr<rhi::RenderDevice> m_RenderDevice;
        std::unique_ptr<rhi::FrameController> m_FrameController;
        std::unique_ptr<rhi::IImGui>   m_ImGuiBackend;
#if defined(VULTRA_ENABLE_XR) && VULTRA_ENABLE_XR
        std::unique_ptr<openxr::XRHeadset> m_XRBackend;
#endif

        rhi::Swapchain m_Swapchain;

        rhi::CommandBuffer* m_ActiveCommandBuffer {nullptr};

        bool     m_XRFrameActive {false};
        bool     m_XRShouldRender {false};
        bool     m_XRMirrorEnabled {false};
        uint32_t m_XRSwapchainImageIndex {0};

        std::vector<XREyeView>    m_XREyeViews;
        std::vector<rhi::Texture> m_XRMirrorTargets;
    };
} // namespace vultra
