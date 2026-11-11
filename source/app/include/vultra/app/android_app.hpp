#pragma once

#include <vultra/app/app_launch.hpp>
#include <vultra/app/render_app.hpp>
#include <vultra/platform/input/input.hpp>

#include <memory>
#include <optional>

namespace vultra
{
    // Borrows GameActivity's glue on its native thread; no Activity or ANativeWindow ownership transfer.
    class AndroidApp : public RenderApp
    {
    public:
        // GPU services and the first surface's swapchain, created before the application so a derived
        // application can create its own GPU objects in its static create(). Moving keeps them at their addresses.
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
            friend class AndroidApp;
            android_app*               m_Activity = nullptr;
            std::unique_ptr<Device>    m_Device;
            std::unique_ptr<Frame>     m_Frame;
            std::unique_ptr<Profiler>  m_Profiler;
            std::unique_ptr<Swapchain> m_Swapchain;
        };

        // Waits for GameActivity's first surface.
        [[nodiscard]] static std::expected<Services, Error> createServices(const AppConfig& config,
                                                                           const AppLaunch& launch);
        AndroidApp(Services services, const AppConfig& config);
        ~AndroidApp() override;
        [[nodiscard]] std::expected<void, Error> run(uint64_t frameLimit = 0) final;

        Device& getDevice();
        // A surface exists from construction and during every render callback; between a surface
        // detach and the next attach there is none, and asking for it is a programming error.
        Swapchain& getSwapchain();
        Input&     getInput();

    private:
        static void                command(android_app* activity, int32_t command);
        std::expected<void, Error> processCommand(int32_t command);
        std::expected<void, Error> attachSurface();
        std::expected<bool, Error> poll(int timeoutMilliseconds);

        android_app&               m_Activity;
        std::unique_ptr<Device>    m_Device;
        std::unique_ptr<Frame>     m_Frame;
        std::unique_ptr<Profiler>  m_Profiler;
        Input                      m_Input;
        VriFormat                  m_Format;
        std::unique_ptr<Swapchain> m_Swapchain;
        bool                       m_Resumed    = false;
        bool                       m_ResetClock = true;
        // A failure inside the GameActivity C callback, reported by the next poll().
        std::optional<Error> m_Failure;
    };
} // namespace vultra
