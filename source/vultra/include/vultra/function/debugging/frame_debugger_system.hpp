#pragma once

#include "vultra/core/engine/engine_subsystem.hpp"
#include "vultra/function/services/frame_debugger_service.hpp"

#include <memory>

namespace vultra
{
    class RenderDocAPI;

    class FrameDebuggerSystem final : public EngineSubsystem, public IFrameDebuggerService
    {
    public:
        ENGINE_SUBSYSTEM(FrameDebuggerSystem)

        // Out-of-line so unique_ptr<RenderDocAPI>'s deleter is instantiated in the .cpp,
        // where RenderDocAPI is a complete type. Both the constructor and destructor must be
        // out-of-line: the implicitly-generated default constructor would otherwise instantiate
        // the member's destructor against the incomplete type at each construction site.
        FrameDebuggerSystem();
        ~FrameDebuggerSystem() override;

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
        std::unique_ptr<RenderDocAPI> m_RenderDocAPI;

        bool m_CaptureRequested {false};
        bool m_ShowCaptureUIRequested {false};
        bool m_RenderDocEnabled {false};
    };
} // namespace vultra
