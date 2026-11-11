#pragma once

#include <vultra/app/app_launch.hpp>
#include <vultra/app/render_app.hpp>
#include <vultra/app/runtime_context.hpp>
#include <vultra/platform/os/window.hpp>

namespace vultra
{
    // Construction cannot fail: createServices() makes the window, device and swapchain first, so a derived
    // application can create its own GPU objects from them in its own static create() before constructing.
    class DesktopApp : public RenderApp
    {
    public:
        using Services = RuntimeContext;

        [[nodiscard]] static std::expected<Services, Error> createServices(const AppConfig& config,
                                                                           const AppLaunch& launch = {});
        DesktopApp(Services services, const AppConfig& config);
        ~DesktopApp() override;
        [[nodiscard]] std::expected<void, Error> run(uint64_t frameLimit = 0) final;

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
