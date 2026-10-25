#pragma once

#include <vultra/main/app/app_launch.hpp>
#include <vultra/main/app/render_app.hpp>
#include <vultra/platform/input/input.hpp>

#include <exception>
#include <memory>

namespace vultra
{
    // Borrows GameActivity's glue on its native thread; no Activity or ANativeWindow ownership transfer.
    class AndroidApp : public RenderApp
    {
    public:
        AndroidApp(const AppConfig& config, const AppLaunch& launch);
        ~AndroidApp() override;
        void run(uint64_t frameLimit = 0) final;

        Device&    getDevice();
        Swapchain& getSwapchain();
        Input&     getInput();

    private:
        static void command(android_app* activity, int32_t command);
        void        processCommand(int32_t command);
        void        attachSurface();
        bool        poll(int timeoutMilliseconds);

        android_app&               m_Activity;
        Device                     m_Device;
        Frame                      m_Frame;
        Profiler                   m_Profiler;
        Input                      m_Input;
        VriFormat                  m_Format;
        std::unique_ptr<Swapchain> m_Swapchain;
        bool                       m_Resumed    = false;
        bool                       m_ResetClock = true;
        std::exception_ptr         m_Failure;
    };
} // namespace vultra
