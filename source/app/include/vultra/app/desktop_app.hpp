#pragma once

#include <vultra/app/app_launch.hpp>
#include <vultra/app/render_app.hpp>
#include <vultra/app/runtime_context.hpp>
#include <vultra/platform/os/window.hpp>

namespace vultra
{
    class DesktopApp : public RenderApp
    {
    public:
        explicit DesktopApp(const AppConfig& config = {}, const AppLaunch& launch = {});
        ~DesktopApp() override;
        void run(uint64_t frameLimit = 0) final;

        Window& getWindow()
        {
            return m_Context.window();
        }

        Device& getDevice()
        {
            return m_Context.device();
        }

        Swapchain& getSwapchain()
        {
            return m_Context.swapchain();
        }

        Input& getInput()
        {
            return getWindow().input();
        }

    private:
        RuntimeContext m_Context;
    };
} // namespace vultra
