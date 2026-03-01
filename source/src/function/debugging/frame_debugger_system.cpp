#include "vultra/function/debugging/frame_debugger_system.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/profiling/renderdoc_api.hpp"

namespace vultra
{
    bool FrameDebuggerSystem::onInit()
    {
        VULTRA_CORE_INFO("[FrameDebuggerSystem] Initializing...");

        VULTRA_CORE_TRACE("[FrameDebuggerSystem] Creating RenderDoc API instance");
        m_RenderDocAPI = new RenderDocAPI();

        VULTRA_CORE_TRACE("[FrameDebuggerSystem] Providing IFrameDebuggerService");
        ctx().services.provide<IFrameDebuggerService>(this);

        return true;
    }

    void FrameDebuggerSystem::onShutdown()
    {
        VULTRA_CORE_INFO("[FrameDebuggerSystem] Shutting down");

        delete m_RenderDocAPI;
        m_RenderDocAPI = nullptr;
    }

    void FrameDebuggerSystem::captureSingleFrame()
    {
        VULTRA_CORE_INFO("[FrameDebuggerSystem] Capturing single frame requested");
<<<<<<< HEAD
        if (m_RenderDocAPI->isAvailable())
        {
            if (!m_RenderDocAPI->isTargetControlConnected() && !m_ShowCaptureUIRequested)
            {
                m_RenderDocAPI->launchReplayUI();
                m_ShowCaptureUIRequested = true;
            }
        }
=======
>>>>>>> 2489d6ae3c882f802a8b749987b772836d053b88
        m_CaptureRequested = true;
    }

    void FrameDebuggerSystem::captureStart()
    {
        if (m_CaptureRequested && !m_RenderDocAPI->isFrameCapturing())
        {
            if (m_RenderDocAPI->isAvailable())
            {
<<<<<<< HEAD
                m_RenderDocAPI->startFrameCapture();
                m_RenderDocAPI->setCaptureTitle("Vultra FrameDebug");
=======
                if (!m_RenderDocAPI->isTargetControlConnected() && !m_ShowCaptureUIRequested)
                {
                    m_RenderDocAPI->launchReplayUI();
                    m_ShowCaptureUIRequested = true;
                }

                if (m_RenderDocAPI->isTargetControlConnected())
                {
                    m_RenderDocAPI->startFrameCapture();
                    m_RenderDocAPI->setCaptureTitle("Vultra FrameDebug");
>>>>>>> 2489d6ae3c882f802a8b749987b772836d053b88

                    VULTRA_CORE_INFO("[FrameDebuggerSystem] Renderdoc Capture started");
                }
            }
            else
            {
                VULTRA_CORE_WARN("[FrameDebuggerSystem] Renderdoc is not available, cannot capture frame");
                m_CaptureRequested = false;
            }
        }
    }

    void FrameDebuggerSystem::captureEnd()
    {
        if (m_CaptureRequested && m_RenderDocAPI->isFrameCapturing())
        {
            if (m_RenderDocAPI->isAvailable())
            {
                m_RenderDocAPI->endFrameCapture();
                m_RenderDocAPI->showReplayUI();
                m_ShowCaptureUIRequested = false;
                m_CaptureRequested       = false;

                VULTRA_CORE_INFO("[FrameDebuggerSystem] Renderdoc Capture ended");
            }
            else
            {
                VULTRA_CORE_WARN("[FrameDebuggerSystem] Renderdoc is not available, cannot end capture");
                m_CaptureRequested = false;
            }
        }
    }
} // namespace vultra