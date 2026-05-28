#include "vultra/core/app/app_host.hpp"
#include "vultra/function/rendering/runtime_profiler.hpp"
#include "vultra/core/base/common_context.hpp"

#include <algorithm>
#include <chrono>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

namespace vultra
{
    bool AppHost::bootstrap()
    {
        if (m_Configured)
        {
            return true;
        }

        // Let derived app configure subsystems/features/plugins
        onConfigure(m_Engine);
        m_Configured = true;
        return true;
    }

    bool AppHost::initCoreIfNeeded()
    {
        if (m_CoreInitialized)
        {
            return true;
        }

        if (!m_Engine.initCore())
        {
            m_ExitCode = 1;
            return false;
        }

        onPostConfigure(m_Engine);
        m_CoreInitialized = true;
        m_LastTick        = std::chrono::steady_clock::now();
        return true;
    }

    bool AppHost::stepFrame()
    {
        RuntimeProfiler::ExternalScope frameScope {"MainLoop::stepFrame"};
        {
            RuntimeProfiler::ExternalScope scope {"MainLoop::pollEvents"};
            onPollEvents();
        }
        if (onShouldClose())
        {
            return false;
        }

        const fsec dt = onFrameDelta();
        {
            RuntimeProfiler::ExternalScope scope {"MainLoop::beforeEngineTick"};
            onBeforeEngineTick(dt);
        }
        {
            RuntimeProfiler::ExternalScope scope {"MainLoop::engineTick"};
            m_Engine.tickFrame(dt);
        }
        {
            RuntimeProfiler::ExternalScope scope {"MainLoop::afterEngineTick"};
            onAfterEngineTick(dt);
        }
        return true;
    }

    void AppHost::shutdownIfNeeded()
    {
        if (m_Shutdown)
        {
            return;
        }
        if (m_CoreInitialized)
        {
            onBeforeShutdown(m_Engine);
            m_Engine.shutdownCore();
        }
        m_Shutdown = true;
    }

#if defined(__EMSCRIPTEN__)
    void AppHost::emscriptenFrameThunk(void* userdata)
    {
        auto* app = static_cast<AppHost*>(userdata);
        if (app != nullptr)
        {
            app->emscriptenFrameStep();
        }
    }

    void AppHost::emscriptenFrameStep()
    {
        try
        {
            if (!initCoreIfNeeded())
            {
                shutdownIfNeeded();
                m_EmscriptenShutdown = true;
                emscripten_cancel_main_loop();
                return;
            }

            if (!stepFrame())
            {
                if (!m_EmscriptenShutdown)
                {
                    shutdownIfNeeded();
                    m_EmscriptenShutdown = true;
                }
                emscripten_cancel_main_loop();
            }
        }
        catch (const std::exception& e)
        {
            VULTRA_CORE_ERROR("[AppHost] Unhandled exception in emscripten frame: {}", e.what());
            m_ExitCode = 1;
            shutdownIfNeeded();
            m_EmscriptenShutdown = true;
            emscripten_cancel_main_loop();
        }
        catch (...)
        {
            VULTRA_CORE_ERROR("[AppHost] Unknown unhandled exception in emscripten frame");
            m_ExitCode = 1;
            shutdownIfNeeded();
            m_EmscriptenShutdown = true;
            emscripten_cancel_main_loop();
        }
    }
#endif

    fsec AppHost::onFrameDelta()
    {
        using Clock = std::chrono::steady_clock;

        const auto now = Clock::now();
        if (m_LastTick.time_since_epoch().count() == 0)
        {
            m_LastTick = now;
            return fsec {0.0f};
        }

        const auto dt = std::chrono::duration_cast<fsec>(now - m_LastTick);
        m_LastTick    = now;

        return fsec {std::clamp(dt.count(), 0.0f, 0.25f)};
    }

    int AppHost::run() { return run(0, nullptr); }

    int AppHost::run(const int argc, char** argv)
    {
        m_Configured      = false;
        m_CoreInitialized = false;
        m_Shutdown        = false;
        m_ExitCode        = 0;
        m_LastTick        = {};
#if defined(__EMSCRIPTEN__)
        m_EmscriptenShutdown = false;
#endif

        m_CommandLineArgs.clear();
        if (argc > 1 && argv != nullptr)
        {
            m_CommandLineArgs.reserve(static_cast<size_t>(argc - 1));
            for (int i = 1; i < argc; ++i)
            {
                if (argv[i] != nullptr)
                {
                    m_CommandLineArgs.emplace_back(argv[i]);
                }
            }
        }

        try
        {
            if (!bootstrap())
            {
                m_ExitCode = 1;
                return m_ExitCode;
            }

#if defined(__EMSCRIPTEN__)
            emscripten_set_main_loop_arg(&AppHost::emscriptenFrameThunk, this, 0, true);
            return 0;
#else
            if (!initCoreIfNeeded())
            {
                shutdownIfNeeded();
                return m_ExitCode;
            }

            while (stepFrame())
            {
            }

            shutdownIfNeeded();
            return m_ExitCode;
#endif
        }
        catch (const std::exception& e)
        {
            VULTRA_CORE_ERROR("[AppHost] Unhandled exception: {}", e.what());
            m_ExitCode = 1;
        }
        catch (...)
        {
            VULTRA_CORE_ERROR("[AppHost] Unknown unhandled exception");
            m_ExitCode = 1;
        }

        shutdownIfNeeded();
        return m_ExitCode;
    }
} // namespace vultra
