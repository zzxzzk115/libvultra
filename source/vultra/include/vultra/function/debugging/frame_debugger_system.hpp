#pragma once

#include "vultra/core/engine/engine_subsystem.hpp"
#include "vultra/function/services/frame_debugger_service.hpp"

namespace vultra
{
    class RenderDocAPI;

    class FrameDebuggerSystem final : public EngineSubsystem, public IFrameDebuggerService
    {
    public:
        ENGINE_SUBSYSTEM(FrameDebuggerSystem)

        bool onInit() override;
        void onShutdown() override;

        void captureSingleFrame() override;
        void showReplayUI() override;

        [[nodiscard]] bool     isAvailable() const override;
        [[nodiscard]] bool     isFrameCapturing() const override;
        [[nodiscard]] uint32_t getCaptureCount() const override;
        [[nodiscard]] bool     isRenderDocEnabled() const override;

    protected:
        void captureStart() override;
        void captureEnd() override;

    private:
        RenderDocAPI* m_RenderDocAPI {nullptr};

        bool m_CaptureRequested {false};
        bool m_ShowCaptureUIRequested {false};
        bool m_RenderDocEnabled {false};
    };
} // namespace vultra
