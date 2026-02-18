#include "vultra/core/os/window_system.hpp"
#include "vultra/core/base/base.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/os/window.hpp"

namespace vultra
{
    bool WindowSystem::onInit()
    {
        auto& cfg = ctx().config;

        m_Window = createRef<os::Window>(
            os::Window::Builder {}.setTitle(cfg.title).setExtent({cfg.windowWidth, cfg.windowHeight}).build());

        ctx().services.provide<IWindowService>(this);

        return true;
    }

    void WindowSystem::onShutdown()
    {
        VULTRA_CORE_INFO("WindowSystem shutting down");
        os::Window::quit(); // SDL_Quit()
    }
} // namespace vultra