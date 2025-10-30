#include "vultra/function/app/base_app.hpp"
#include "vultra/core/input/input.hpp"
#include "vultra/function/service/services.hpp"

namespace vultra
{
    BaseApp::BaseApp(std::span<char*>, const AppConfig& cfg) :
        m_RenderDocAPI(std::make_unique<RenderDocAPI>()),
        m_Window(os::Window::Builder {}.setExtent({cfg.width, cfg.height}).build()),
        m_RenderDevice(std::make_unique<rhi::RenderDevice>(cfg.renderDeviceFeatureFlag))
    {
        m_Window.setTitle(std::format("{} ({})", cfg.title, m_RenderDevice->getName()));

        m_Swapchain       = m_RenderDevice->createSwapchain(m_Window, cfg.swapchainFormat, cfg.vSyncConfig);
        m_FrameController = rhi::FrameController {*m_RenderDevice, m_Swapchain, cfg.numFramesInFlight};

        setupWindowCallbacks();

        service::Services::init(*m_RenderDevice);
    }

    os::Window& BaseApp::getWindow() { return m_Window; }

    rhi::RenderDevice& BaseApp::getRenderDevice() { return *m_RenderDevice; }

    rhi::Swapchain& BaseApp::getSwapchain() { return m_Swapchain; }

    void BaseApp::run()
    {
        FPSMonitor fpsMonitor {m_Window};

        const fsec targetFrameTime {1.0 / 60.0f};
        fsec       deltaTime {targetFrameTime};
        fsec       accumulator {0};

        // https://gafferongames.com/post/fix_your_timestep/
        // http://gameprogrammingpatterns.com/game-loop.html
        // https://dewitters.com/dewitters-gameloop/
        // http://higherorderfun.com/blog/2010/08/17/understanding-the-game-main-loop/

        while (!m_Window.shouldClose())
        {
            using clock           = std::chrono::high_resolution_clock;
            const auto beginTicks = clock::now();

            {
                ZoneScopedN("[App] PreUpdate");
                onPreUpdate(deltaTime);
            }
            {
                ZoneScopedN("[App] PollEvents");
                m_Window.pollEvents();
            }
            if (!m_IsRunning)
                break;

            {
                ZoneScopedN("[App] Update");
                onUpdate(deltaTime);
            }
            {
                ZoneScopedN("[App] PhysicsUpdate");
                accumulator += (deltaTime < targetFrameTime ? deltaTime : targetFrameTime);
                while (accumulator >= targetFrameTime)
                {
                    onPhysicsUpdate(targetFrameTime);
                    accumulator -= targetFrameTime;
                }
            }
            {
                ZoneScopedN("[App] PostUpdate");
                onPostUpdate(deltaTime);
            }

            if (m_Swapchain && !m_Minimized)
            {
                {
                    ZoneScopedN("[App] PreRender");
                    onPreRender();
                }
                {
                    ZoneScopedN("[App] Render");

                    renderDocCaptureBegin();

                    bool valid = m_FrameController.acquireNextFrame();
                    if (!valid)
                    {
                        // If we failed to acquire the next frame, skip rendering and presenting
                        continue;
                    }

                    auto& cb = m_FrameController.beginFrame();

                    onRender(cb, m_FrameController.getCurrentTarget(), deltaTime);

                    m_FrameController.endFrame();

                    renderDocCaptureEnd();
                }
                {
                    ZoneScopedN("[App] PostRender");
                    onPostRender();
                }
                {
                    ZoneScopedN("[App] Present");
                    m_FrameController.present();
                    m_FrameCounter++;
                }
            }
            FrameMark;

            deltaTime = clock::now() - beginTicks;
            if (deltaTime > 1s)
                deltaTime = targetFrameTime;

            fpsMonitor.update(deltaTime);
        }

        m_RenderDevice->waitIdle();
        service::Services::reset();
    }

    void BaseApp::close() { m_IsRunning = false; }

    void BaseApp::setupWindowCallbacks()
    {
        m_Window.on<os::GeneralWindowEvent>(
            [this](const os::GeneralWindowEvent& event, os::Window&) { onGeneralWindowEvent(event); });
    }

    void BaseApp::onGeneralWindowEvent(const os::GeneralWindowEvent& event)
    {
        // Keyboard events
        if (event.type == SDL_EVENT_KEY_DOWN)
        {
            if (event.internalEvent.key.repeat == 0)
            {
                Input::setKeyState(event.internalEvent.key.scancode, InputAction::ePress);
            }
            else
            {
                Input::setKeyState(event.internalEvent.key.scancode, InputAction::eRepeat);
            }
        }
        else if (event.type == SDL_EVENT_KEY_UP)
        {
            Input::setKeyState(event.internalEvent.key.scancode, InputAction::eRelease);
        }

        // Mouse button events
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
        {
            Input::setMouseButtonState(event.internalEvent.button.button,
                                       {.pressed = true, .clicks = event.internalEvent.button.clicks});
        }
        else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP)
        {
            Input::setMouseButtonState(event.internalEvent.button.button,
                                       {.pressed = false, .clicks = event.internalEvent.button.clicks});
        }
        else if (event.type == SDL_EVENT_MOUSE_MOTION)
        {
            Input::setMousePosition({event.internalEvent.motion.x, event.internalEvent.motion.y});
            Input::setMousePositionFlipY({event.internalEvent.motion.x,
                                          static_cast<float>(m_Window.getExtent().y) - event.internalEvent.motion.y});
            Input::setMousePositionDelta({event.internalEvent.motion.xrel, event.internalEvent.motion.yrel});
        }
        else if (event.type == SDL_EVENT_MOUSE_WHEEL)
        {
            Input::setMouseScrollDelta({event.internalEvent.wheel.x, event.internalEvent.wheel.y});
        }

        // Window events
        if (event.type == SDL_EVENT_WINDOW_RESIZED)
        {
            onResize(event.internalEvent.window.data1, event.internalEvent.window.data2);
        }
        else if (event.type == SDL_EVENT_WINDOW_MINIMIZED)
        {
            m_Minimized = true;
        }
        else if (event.type == SDL_EVENT_WINDOW_RESTORED)
        {
            m_Minimized = false;
            onResize(m_Window.getExtent().x, m_Window.getExtent().y);
        }
    }

    void BaseApp::onResize(uint32_t width, uint32_t height)
    {
        // Only recreate the swapchain for Wayland, as X11 handles it automatically
        if (m_Window.getDriverType() == os::Window::DriverType::eWayland)
        {
            // Skip zero extent
            if (width == 0 || height == 0)
                return;
            m_Swapchain.recreate();
        }
    }

    void BaseApp::onPreUpdate(const fsec /*dt*/) { Input::clearStates(); }

    void BaseApp::renderDocCaptureBegin()
    {
        if (m_WantCaptureFrame && !m_RenderDocAPI->isFrameCapturing())
        {
            if (m_RenderDocAPI->isAvailable())
            {
                if (!m_RenderDocAPI->isTargetControlConnected())
                {
                    m_RenderDocAPI->launchReplayUI();
                }

                m_RenderDocAPI->startFrameCapture();
                m_RenderDocAPI->setCaptureTitle(fmt::format("Vultra Frame#{}", m_FrameCounter));

                VULTRA_CORE_INFO("[App] Renderdoc Capture started");
            }
            else
            {
                VULTRA_CORE_WARN("[App] Renderdoc is not available, cannot capture frame");
            }
        }
    }

    void BaseApp::renderDocCaptureEnd()
    {
        if (m_WantCaptureFrame && m_RenderDocAPI->isFrameCapturing())
        {
            if (m_RenderDocAPI->isAvailable())
            {
                m_RenderDocAPI->endFrameCapture();
                m_RenderDocAPI->showReplayUI();
                m_WantCaptureFrame = false;

                VULTRA_CORE_INFO("[App] Renderdoc Capture ended");
            }
            else
            {
                VULTRA_CORE_WARN("[App] Renderdoc is not available, cannot end capture");
            }
        }
    }
} // namespace vultra