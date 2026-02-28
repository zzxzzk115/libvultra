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

        void onPreRender() override;
        void onPostRender() override;

        void captureSingleFrame() override;

    private:
        RenderDocAPI* m_RenderDocAPI {nullptr};

        bool m_CaptureRequested {false};
    };
} // namespace vultra