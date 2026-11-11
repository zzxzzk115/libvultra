#pragma once

#include <vultra/app/app_launch.hpp>
#include <vultra/app/render_app.hpp>
#include <vultra/platform/web/input.hpp>

#include <memory>
#include <optional>
#include <string>

namespace vultra
{
    // run() yields to one browser animation frame at a time; ASYNCIFY keeps the C++ owner alive.
    class WebApp : public RenderApp
    {
    public:
        // GPU services, input and the canvas swapchain, created before the application so a derived application
        // can create its own GPU objects in its static create(). Moving keeps them at their addresses.
        class Services
        {
        public:
            Device& device()
            {
                return *m_Device;
            }

            Swapchain& swapchain()
            {
                return *m_Swapchain;
            }

        private:
            friend class WebApp;
            // The swapchain keeps the selector's address, so the string lives on the heap.
            std::unique_ptr<const std::string> m_Canvas;
            std::unique_ptr<Device>            m_Device;
            std::unique_ptr<Frame>             m_Frame;
            std::unique_ptr<Profiler>          m_Profiler;
            std::optional<platform::WebInput>  m_Input;
            std::unique_ptr<Swapchain>         m_Swapchain;
        };

        [[nodiscard]] static std::expected<Services, Error> createServices(const AppConfig& config = {},
                                                                           const AppLaunch& launch = {});
        WebApp(Services services, const AppConfig& config);
        ~WebApp() override;
        [[nodiscard]] std::expected<void, Error> run(uint64_t frameLimit = 0) final;
        Device&                                  getDevice();
        Swapchain&                               getSwapchain();
        Input&                                   getInput();

    private:
        std::unique_ptr<const std::string> m_Canvas;
        std::unique_ptr<Device>            m_Device;
        std::unique_ptr<Frame>             m_Frame;
        std::unique_ptr<Profiler>          m_Profiler;
        platform::WebInput                 m_Input;
        std::unique_ptr<Swapchain>         m_Swapchain;
        std::optional<Texture>             m_Target;
    };
} // namespace vultra
