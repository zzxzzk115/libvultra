#include <vultra/app/desktop_app.hpp>

#include <chrono>

namespace vultra
{
    std::expected<DesktopApp::Services, Error> DesktopApp::createServices(const AppConfig& config, const AppLaunch&)
    {
        return RuntimeContext::create(config);
    }

    DesktopApp::DesktopApp(Services services, const AppConfig& config) :
        RenderApp(config),
        m_Context(std::move(services))
    {
    }

    DesktopApp::~DesktopApp() = default;

    std::expected<void, Error> DesktopApp::run(uint64_t frameLimit)
    {
        auto& window   = m_Context.window();
        auto  previous = std::chrono::steady_clock::now();
        if (auto reset = resetFrames(); !reset)
        {
            return reset;
        }
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
            const auto suffix       = advanceFrame(m_Context.device(),
                                                   m_Context.swapchain(),
                                                   m_Context.frame(),
                                                   m_Context.profiler(),
                                                   deltaSeconds,
                                                   !window.minimized());
            if (!suffix)
            {
                return std::unexpected(suffix.error());
            }
            if (*suffix)
            {
                window.setTitleSuffix(std::move(**suffix));
            }
            if (m_FrameCount == completed && !m_CloseRequested)
            {
                Window::waitEvents(0.01);
            }
        }
        return {};
    }
} // namespace vultra
