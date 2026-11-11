#include <vultra/app/android_app.hpp>
#include <vultra/core/base/logger.hpp>
#include <vultra/platform/android/input.hpp>

#include <android/native_window.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>

#include <chrono>

namespace vultra
{
    namespace
    {
        Extent surfaceSize(const android_app& activity)
        {
            if (!activity.window)
            {
                return {};
            }
            return {uint32_t(ANativeWindow_getWidth(activity.window)),
                    uint32_t(ANativeWindow_getHeight(activity.window))};
        }

        // Returns whether an event was processed.
        std::expected<bool, Error> pollLooper(android_app& activity, int timeoutMilliseconds)
        {
            android_poll_source* source = nullptr;
            const auto           event =
                ALooper_pollOnce(timeoutMilliseconds, nullptr, nullptr, reinterpret_cast<void**>(&source));
            if (event == ALOOPER_POLL_ERROR)
            {
                return makeError("Poll GameActivity looper failed");
            }
            if (source)
            {
                source->process(&activity, source);
            }
            return event >= 0;
        }

        std::expected<std::unique_ptr<Swapchain>, Error>
        createSurfaceSwapchain(Device& device, android_app& activity, VriFormat format)
        {
            const auto size = surfaceSize(activity);
            if (size.empty())
            {
                return nullptr;
            }
            VriWindowHandle window {};
            window.type                  = VriWindowSystem_Android;
            window.handle.android.window = activity.window;
            auto swapchain               = Swapchain::create(device, window, size, format);
            if (!swapchain)
            {
                return std::unexpected(swapchain.error());
            }
            Logger::core().debug("Android surface attached: {}x{}", size.width, size.height);
            return std::move(*swapchain);
        }
    } // namespace

    std::expected<AndroidApp::Services, Error> AndroidApp::createServices(const AppConfig& config,
                                                                          const AppLaunch& launch)
    {
        if (!launch.activity)
        {
            return makeError("AndroidApp requires GameActivity entry state");
        }
        auto& activity = *launch.activity;
        if (activity.userData || activity.onAppCmd)
        {
            return makeError("AndroidApp requires exclusive ownership of GameActivity's native callbacks");
        }
        Services services;
        services.m_Activity = &activity;
        auto device         = Device::create(config.validation, nullptr, config.features);
        if (!device)
        {
            return std::unexpected(device.error());
        }
        services.m_Device = std::move(*device);
        auto frame        = Frame::create(*services.m_Device);
        if (!frame)
        {
            return std::unexpected(frame.error());
        }
        services.m_Frame = std::move(*frame);
        auto profiler    = Profiler::create(*services.m_Device);
        if (!profiler)
        {
            return std::unexpected(profiler.error());
        }
        services.m_Profiler = std::move(*profiler);
        // Wait before installing callbacks: derived GPU objects may use the swapchain before construction.
        while (!activity.window && !activity.destroyRequested)
        {
            if (auto polled = pollLooper(activity, -1); !polled)
            {
                return std::unexpected(polled.error());
            }
        }
        if (activity.destroyRequested)
        {
            return makeError("GameActivity was destroyed before its surface became available");
        }
        auto swapchain = createSurfaceSwapchain(*services.m_Device, activity, config.swapchainFormat);
        if (!swapchain)
        {
            return std::unexpected(swapchain.error());
        }
        if (!*swapchain)
        {
            return makeError("GameActivity reported an empty surface");
        }
        services.m_Swapchain = std::move(*swapchain);
        return services;
    }

    AndroidApp::AndroidApp(Services services, const AppConfig& config) :
        RenderApp(config),
        m_Activity(*services.m_Activity),
        m_Device(std::move(services.m_Device)),
        m_Frame(std::move(services.m_Frame)),
        m_Profiler(std::move(services.m_Profiler)),
        m_Format(config.swapchainFormat),
        m_Swapchain(std::move(services.m_Swapchain))
    {
        m_Resumed           = m_Activity.activityState == APP_CMD_RESUME;
        m_Activity.userData = this;
        m_Activity.onAppCmd = command;
        android_app_set_motion_event_filter(&m_Activity, nullptr);
        android_app_set_key_event_filter(&m_Activity, nullptr);
    }

    AndroidApp::~AndroidApp()
    {
        m_Activity.onAppCmd = nullptr;
        m_Activity.userData = nullptr;
        m_Device->waitIdleForDestruction();
        m_Swapchain.reset();
    }

    std::expected<void, Error> AndroidApp::attachSurface()
    {
        auto swapchain = createSurfaceSwapchain(*m_Device, m_Activity, m_Format);
        if (!swapchain)
        {
            return std::unexpected(swapchain.error());
        }
        if (*swapchain)
        {
            m_Swapchain = std::move(*swapchain);
            invalidateSurface();
        }
        return {};
    }

    void AndroidApp::command(android_app* activity, int32_t event)
    {
        auto& app = *static_cast<AndroidApp*>(activity->userData);
        // Errors must not cross the GameActivity C callback or its surface-destruction handshake.
        if (auto processed = app.processCommand(event); !processed && !app.m_Failure)
        {
            app.m_Failure = processed.error();
            app.close();
        }
    }

    std::expected<void, Error> AndroidApp::processCommand(int32_t event)
    {
        switch (event)
        {
            case APP_CMD_INIT_WINDOW:
                return attachSurface();
            case APP_CMD_TERM_WINDOW: {
                // Glue releases the ANativeWindow after this callback acknowledges the command, so the
                // swapchain is released even when the GPU wait fails.
                const auto idle = m_Device->waitIdle();
                m_Swapchain.reset();
                m_Input.setFocused(false);
                Logger::core().debug("Android surface detached after GPU completion");
                return idle;
            }
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
        return {};
    }

    std::expected<bool, Error> AndroidApp::poll(int timeoutMilliseconds)
    {
        const auto polled = pollLooper(m_Activity, timeoutMilliseconds);
        if (m_Failure)
        {
            return std::unexpected(*m_Failure);
        }
        return polled;
    }

    std::expected<void, Error> AndroidApp::run(uint64_t frameLimit)
    {
        if (auto reset = resetFrames(); !reset)
        {
            return reset;
        }
        auto previous = std::chrono::steady_clock::now();
        while (!m_CloseRequested && !m_Activity.destroyRequested && (!frameLimit || m_FrameCount < frameLimit))
        {
            auto polled = poll(m_Resumed && m_Swapchain ? 0 : -1);
            while (polled && *polled)
            {
                polled = poll(0);
            }
            if (!polled)
            {
                return std::unexpected(polled.error());
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
            if (auto resized = m_Swapchain->resize(surfaceSize(m_Activity)); !resized)
            {
                return resized;
            }
            if (auto advanced = advanceFrame(*m_Device, *m_Swapchain, *m_Frame, *m_Profiler, deltaSeconds); !advanced)
            {
                return std::unexpected(advanced.error());
            }
        }
        return m_Device->waitIdle();
    }

    Device& AndroidApp::getDevice()
    {
        return *m_Device;
    }

    Swapchain& AndroidApp::getSwapchain()
    {
        if (!m_Swapchain)
        {
            panic("GameActivity has no active surface");
        }
        return *m_Swapchain;
    }

    Input& AndroidApp::getInput()
    {
        return m_Input;
    }
} // namespace vultra
