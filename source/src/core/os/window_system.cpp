#include "vultra/core/os/window_system.hpp"
#include "vultra/core/base/base.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/os/window.hpp"

namespace vultra
{
    bool WindowSystem::onInit()
    {
        VULTRA_CORE_INFO("[WindowSystem] Initializing...");

        VULTRA_CORE_TRACE("[WindowSystem] Creating window");
        auto& cfg = ctx().config;

        m_Window = createRef<os::Window>(
            os::Window::Builder {}.setTitle(cfg.title).setExtent({cfg.windowWidth, cfg.windowHeight}).build());

        VULTRA_CORE_TRACE("[WindowSystem] Providing IWindowService");
        ctx().services.provide<IWindowService>(this);

        VULTRA_CORE_INFO("[WindowSystem] Initialized!");
        return true;
    }

    void WindowSystem::onShutdown()
    {
        VULTRA_CORE_INFO("[WindowSystem] Shutting down");
        os::Window::quit(); // SDL_Quit()
    }
} // namespace vultra