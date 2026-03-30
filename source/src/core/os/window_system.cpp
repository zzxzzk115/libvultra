#include "vultra/core/os/window_system.hpp"
#include "vultra/core/base/base.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/os/window.hpp"

#if defined(__ANDROID__)
#include "vultra/platform/android/android_native_window.hpp"
#endif

namespace vultra
{
    bool WindowSystem::onInit()
    {
        VULTRA_CORE_INFO("[WindowSystem] Initializing...");

        VULTRA_CORE_TRACE("[WindowSystem] Creating window");
        auto& cfg = ctx().config;

#if defined(__ANDROID__)
        if (cfg.window.android.app != nullptr)
        {
            m_Window = std::make_shared<platform::android::AndroidNativeWindow>(*cfg.window.android.app);
        }
        else
        {
            m_Window = std::make_shared<platform::android::AndroidNativeWindow>(cfg.window.android.nativeWindow,
                                                                                cfg.window.android.destroyRequested);
        }
        m_Window->setTitle(cfg.window.title);
#else
        m_Window = os::Window::Builder {}
                       .setTitle(cfg.window.title)
                       .setExtent({static_cast<int>(cfg.window.width), static_cast<int>(cfg.window.height)})
                       .setResizable(cfg.window.resizable)
                       .setFullscreen(cfg.window.fullscreen)
                       .build();
#endif

        VULTRA_CORE_TRACE("[WindowSystem] Providing IWindowService");
        ctx().services.provide<IWindowService>(this);

        VULTRA_CORE_INFO("[WindowSystem] Initialized!");
        return true;
    }

    void WindowSystem::onShutdown()
    {
        VULTRA_CORE_INFO("[WindowSystem] Shutting down");
        os::Window::shutdownPlatform();
    }
} // namespace vultra
