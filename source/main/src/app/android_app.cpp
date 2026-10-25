#include <vultra/core/base/logger.hpp>
#include <vultra/main/app/android_app.hpp>
#include <vultra/platform/android/input.hpp>

#include <android/native_window.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>

#include <chrono>
#include <stdexcept>

namespace vultra
{
    namespace
    {
        android_app& activityFor(const AppLaunch& launch)
        {
            if (!launch.activity)
            {
                throw std::invalid_argument("AndroidApp requires GameActivity entry state");
            }
            return *launch.activity;
        }

        Extent surfaceSize(const android_app& activity)
        {
            if (!activity.window)
            {
                return {};
            }
            return {uint32_t(ANativeWindow_getWidth(activity.window)),
                    uint32_t(ANativeWindow_getHeight(activity.window))};
        }
    } // namespace

    AndroidApp::AndroidApp(const AppConfig& config, const AppLaunch& launch) :
        RenderApp(config),
        m_Activity(activityFor(launch)),
        m_Device(config.validation, nullptr, config.features),
        m_Frame(m_Device),
        m_Profiler(m_Device),
        m_Format(config.swapchainFormat)
    {
        auto& activity = m_Activity;
        if (activity.userData || activity.onAppCmd)
        {
            throw std::logic_error("AndroidApp requires exclusive ownership of GameActivity's native callbacks");
        }
        // Wait before installing callbacks: derived GPU members may use the swapchain in their constructors.
        while (!activity.window && !activity.destroyRequested)
        {
            poll(-1);
        }
        if (activity.destroyRequested)
        {
            throw std::runtime_error("GameActivity was destroyed before its surface became available");
        }
        attachSurface();
        m_Resumed         = activity.activityState == APP_CMD_RESUME;
        activity.userData = this;
        activity.onAppCmd = command;
        android_app_set_motion_event_filter(&activity, nullptr);
        android_app_set_key_event_filter(&activity, nullptr);
    }

    AndroidApp::~AndroidApp()
    {
        m_Activity.onAppCmd = nullptr;
        m_Activity.userData = nullptr;
        m_Device.waitIdleForDestruction();
        m_Swapchain.reset();
    }

    void AndroidApp::attachSurface()
    {
        const auto size = surfaceSize(m_Activity);
        if (!size.empty())
        {
            VriWindowHandle window {};
            window.type                  = VriWindowSystem_Android;
            window.handle.android.window = m_Activity.window;
            m_Swapchain                  = std::make_unique<Swapchain>(m_Device, window, size, m_Format);
            invalidateSurface();
            Logger::core().debug("Android surface attached: {}x{}", size.width, size.height);
        }
    }

    void AndroidApp::command(android_app* activity, int32_t event)
    {
        auto& app = *static_cast<AndroidApp*>(activity->userData);
        try
        {
            app.processCommand(event);
        }
        catch (...)
        {
            // Exceptions must not cross the GameActivity C callback or its surface-destruction handshake.
            app.m_Failure = std::current_exception();
            app.close();
        }
    }

    void AndroidApp::processCommand(int32_t event)
    {
        switch (event)
        {
            case APP_CMD_INIT_WINDOW:
                attachSurface();
                break;
            case APP_CMD_TERM_WINDOW:
                // Glue releases the ANativeWindow after this callback acknowledges the command.
                m_Device.waitIdle();
                m_Swapchain.reset();
                m_Input.setFocused(false);
                Logger::core().debug("Android surface detached after GPU completion");
                break;
            case APP_CMD_RESUME:
                m_Resumed    = true;
                m_ResetClock = true;
                Logger::core().debug("Android activity resumed");
                break;
            case APP_CMD_PAUSE:
            case APP_CMD_STOP:
                m_Resumed = false;
                m_Input.setFocused(false);
                Logger::core().debug("Android activity paused or stopped");
                break;
            case APP_CMD_GAINED_FOCUS:
                m_Input.setFocused(true);
                break;
            case APP_CMD_LOST_FOCUS:
                m_Input.setFocused(false);
                break;
            default:
                break;
        }
    }

    bool AndroidApp::poll(int timeoutMilliseconds)
    {
        android_poll_source* source = nullptr;
        const auto event = ALooper_pollOnce(timeoutMilliseconds, nullptr, nullptr, reinterpret_cast<void**>(&source));
        if (event == ALOOPER_POLL_ERROR)
        {
            throw std::runtime_error("Poll GameActivity looper failed");
        }
        if (source)
        {
            source->process(&m_Activity, source);
        }
        if (m_Failure)
        {
            std::rethrow_exception(m_Failure);
        }
        return event >= 0;
    }

    void AndroidApp::run(uint64_t frameLimit)
    {
        resetFrames();
        auto previous = std::chrono::steady_clock::now();
        while (!m_CloseRequested && !m_Activity.destroyRequested && (!frameLimit || m_FrameCount < frameLimit))
        {
            poll(m_Resumed && m_Swapchain ? 0 : -1);
            while (poll(0))
            {
            }
            const auto now          = std::chrono::steady_clock::now();
            const auto deltaSeconds = m_ResetClock ? 0 : std::chrono::duration<float>(now - previous).count();
            previous                = now;
            platform::processAndroidInput(m_Input, m_Activity);
            m_Input.advanceFrame();
            if (!m_Resumed || !m_Swapchain || m_Activity.destroyRequested || m_CloseRequested)
            {
                continue;
            }
            m_ResetClock = false;
            m_Swapchain->resize(surfaceSize(m_Activity));
            advanceFrame(m_Device, *m_Swapchain, m_Frame, m_Profiler, deltaSeconds);
        }
        m_Device.waitIdle();
    }

    Device& AndroidApp::getDevice()
    {
        return m_Device;
    }

    Swapchain& AndroidApp::getSwapchain()
    {
        if (!m_Swapchain)
        {
            throw std::logic_error("GameActivity has no active surface");
        }
        return *m_Swapchain;
    }

    Input& AndroidApp::getInput()
    {
        return m_Input;
    }
} // namespace vultra
