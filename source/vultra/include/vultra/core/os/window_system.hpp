// window_system.hpp

#pragma once

#include "vultra/core/engine/engine_subsystem.hpp"
#include "vultra/core/services/window_service.hpp"

#include <memory>

namespace vultra
{
    class WindowSystem : public vultra::EngineSubsystem, public IWindowService
    {
    public:
        ENGINE_SUBSYSTEM(WindowSystem)

        vultra::os::Window& window() override { return *m_Window; }

    protected:
        bool onInit() override;
        void onShutdown() override;

    private:
        std::shared_ptr<vultra::os::Window> m_Window {nullptr};
    };
} // namespace vultra
