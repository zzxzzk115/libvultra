#include <vultra/main/app/desktop_app.hpp>

#include <chrono>

namespace vultra
{
    DesktopApp::DesktopApp(const AppConfig& config, const AppLaunch&) :
        RenderApp(config),
        m_Context(config)
    {
    }

    DesktopApp::~DesktopApp() = default;

    void DesktopApp::run(uint64_t frameLimit)
    {
        auto& window   = m_Context.window();
        auto  previous = std::chrono::steady_clock::now();
        resetFrames();
        while (!m_CloseRequested && (!frameLimit || m_FrameCount < frameLimit))
        {
            if (!window.poll())
            {
                break;
            }
            if (window.input().isKeyHeld(KeyCode::eEscape))
            {
                close();
                break;
            }
            const auto now          = std::chrono::steady_clock::now();
            const auto deltaSeconds = std::chrono::duration<float>(now - previous).count();
            previous                = now;
            const auto completed    = m_FrameCount;
            if (auto suffix = advanceFrame(m_Context.device(),
                                           m_Context.swapchain(),
                                           m_Context.frame(),
                                           m_Context.profiler(),
                                           deltaSeconds,
                                           !window.minimized()))
            {
                window.setTitleSuffix(std::move(*suffix));
            }
            if (m_FrameCount == completed && !m_CloseRequested)
            {
                Window::waitEvents(0.01);
            }
        }
    }
} // namespace vultra
