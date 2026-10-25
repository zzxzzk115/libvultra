#pragma once

#include <vultra/main/app/app_launch.hpp>
#include <vultra/main/app/render_app.hpp>
#include <vultra/platform/web/input.hpp>

#include <memory>
#include <string>

namespace vultra
{
    // run() yields to one browser animation frame at a time; ASYNCIFY keeps the C++ owner alive.
    class WebApp : public RenderApp
    {
    public:
        explicit WebApp(const AppConfig& config = {}, const AppLaunch& launch = {});
        ~WebApp() override;
        void       run(uint64_t frameLimit = 0) final;
        Device&    getDevice();
        Swapchain& getSwapchain();
        Input&     getInput();

    private:
        std::string                m_Canvas;
        Device                     m_Device;
        Frame                      m_Frame;
        Profiler                   m_Profiler;
        platform::WebInput         m_Input;
        std::unique_ptr<Swapchain> m_Swapchain;
        std::unique_ptr<Texture>   m_Target;
    };
} // namespace vultra
