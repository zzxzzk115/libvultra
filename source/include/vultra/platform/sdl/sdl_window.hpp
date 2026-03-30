#pragma once

#if !defined(__ANDROID__)

#include "vultra/core/os/window.hpp"

struct SDL_Window;

namespace vultra::platform::sdl
{
    class SDLWindow final : public os::Window
    {
    public:
        SDLWindow(std::string_view title,
                  Extent           extent,
                  Position         position,
                  bool             cursorVisible,
                  bool             resizable,
                  bool             fullscreen);
        ~SDLWindow() override;

        [[nodiscard]] PlatformType platformType() const override { return PlatformType::eSDL3; }
        [[nodiscard]] DriverType   driverType() const override;

        os::Window& setTitle(std::string_view title) override;
        os::Window& setExtent(Extent extent) override;
        os::Window& setPosition(Position position) override;
        os::Window& setCursor(CursorType cursor) override;
        os::Window& setCursorVisibility(bool cursorVisibility) override;
        os::Window& setMouseRelativeMode(bool mouseRelativeMode) override;
        os::Window& setResizable(bool resizable) override;
        os::Window& setFullscreen(bool fullscreen) override;

        [[nodiscard]] std::string_view getTitle() const override { return m_Title; }
        [[nodiscard]] Extent           getExtent() const override { return m_Extent; }
        [[nodiscard]] Extent           getFrameBufferExtent() const override { return m_FrameBufferExtent; }
        [[nodiscard]] Position         getPosition() const override { return m_Position; }
        [[nodiscard]] CursorType       getCursor() const override { return m_Cursor; }
        [[nodiscard]] bool             getCursorVisibility() const override { return m_CursorVisibility; }
        [[nodiscard]] bool             getMouseRelativeMode() const override { return m_MouseRelativeMode; }
        [[nodiscard]] bool             isResizable() const override { return m_Resizable; }
        [[nodiscard]] bool             isFullscreen() const override { return m_Fullscreen; }
        [[nodiscard]] float            getDisplayScale() const override;
        [[nodiscard]] bool             shouldClose() const override { return m_ShouldClose; }
        [[nodiscard]] bool             isMinimized() const override { return m_IsMinimized; }
        [[nodiscard]] bool             isReady() const override { return true; }

        [[nodiscard]] std::span<const char* const> getRequiredVulkanInstanceExtensions() const override;
        [[nodiscard]] vk::SurfaceKHR               createVulkanSurface(vk::Instance instance) const override;

        void pollEvents(int timeoutMillis = 0) override;
        void close() override;

        [[nodiscard]] SDL_Window* getHandle() const { return m_WindowHandle; }

        static void shutdown();

    private:
        [[nodiscard]] static DriverType translateDriverType();
        [[nodiscard]] static KeyCode    translateKeyCode(int scancode);
        [[nodiscard]] static MouseCode  translateMouseCode(uint8_t button);

    private:
        std::string m_Title;
        Extent      m_Extent {};
        Extent      m_FrameBufferExtent {};
        Position    m_Position {};
        CursorType  m_Cursor {CursorType::eArrow};
        bool        m_CursorVisibility {true};
        bool        m_MouseRelativeMode {false};
        bool        m_Resizable {true};
        bool        m_Fullscreen {false};
        bool        m_ShouldClose {false};
        bool        m_IsMinimized {false};

        SDL_Window*              m_WindowHandle {nullptr};
        std::vector<const char*> m_VulkanExtensions;
    };
} // namespace vultra::platform::sdl

#endif
