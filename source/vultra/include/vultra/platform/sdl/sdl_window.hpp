#pragma once

#if !defined(__ANDROID__)

#include "vultra/core/os/window.hpp"

#include <algorithm>
#include <array>
#include <optional>

struct SDL_Window;
struct SDL_Cursor;
struct SDL_Gamepad;

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
                  bool             fullscreen,
                  bool             decorated,
                  bool             visible);
        ~SDLWindow() override;

        [[nodiscard]] PlatformType platformType() const override { return PlatformType::eSDL3; }
        [[nodiscard]] DriverType   driverType() const override;

        os::Window& setTitle(std::string_view title) override;
        os::Window& setExtent(Extent extent) override;
        os::Window& setPosition(Position position) override;
        os::Window& setCursor(CursorType cursor) override;
        os::Window& setCustomCursor(const CursorImage& cursorImage) override;
        os::Window& clearCustomCursor() override;
        os::Window& setCursorOverride(const CursorImage& cursorImage) override;
        os::Window& clearCursorOverride() override;
        os::Window& setCursorVisibility(bool cursorVisibility) override;
        os::Window& setMouseRelativeMode(bool mouseRelativeMode) override;
        os::Window& setResizable(bool resizable) override;
        os::Window& setFullscreen(bool fullscreen) override;
        os::Window& setDecorated(bool decorated) override;
        os::Window& setVisible(bool visible) override;
        os::Window& centerOnScreen() override;

        [[nodiscard]] std::string_view getTitle() const override { return m_Title; }
        [[nodiscard]] Extent           getExtent() const override { return m_Extent; }
        [[nodiscard]] Extent           getFrameBufferExtent() const override { return m_FrameBufferExtent; }
        [[nodiscard]] rhi::Rect2D      getContentArea() const override
        {
            return rhi::Rect2D {.offset = {0, 0},
                                .extent = {static_cast<uint32_t>(std::max(m_FrameBufferExtent.x, 0)),
                                           static_cast<uint32_t>(std::max(m_FrameBufferExtent.y, 0))}};
        }
        [[nodiscard]] Position         getPosition() const override { return m_Position; }
        [[nodiscard]] CursorType       getCursor() const override { return m_Cursor; }
        [[nodiscard]] bool             hasCustomCursor() const override { return m_HasCustomCursor; }
        [[nodiscard]] bool             hasCursorOverride() const override { return m_HasCursorOverride; }
        [[nodiscard]] bool             getCursorVisibility() const override { return m_CursorVisibility; }
        [[nodiscard]] bool             getMouseRelativeMode() const override { return m_MouseRelativeMode; }
        [[nodiscard]] bool             isResizable() const override { return m_Resizable; }
        [[nodiscard]] bool             isFullscreen() const override { return m_Fullscreen; }
        [[nodiscard]] bool             isDecorated() const override { return m_Decorated; }
        [[nodiscard]] bool             isVisible() const override { return m_Visible; }
        [[nodiscard]] float            getDisplayScale() const override;
        [[nodiscard]] bool             shouldClose() const override { return m_ShouldClose; }
        [[nodiscard]] bool             isMinimized() const override { return m_IsMinimized; }
        [[nodiscard]] bool             isMaximized() const override;
        [[nodiscard]] bool             isReady() const override { return true; }

#if defined(VULTRA_ENABLE_VULKAN) && VULTRA_ENABLE_VULKAN
        [[nodiscard]] std::span<const char* const> getRequiredVulkanInstanceExtensions() const override;
        [[nodiscard]] vk::SurfaceKHR               createVulkanSurface(vk::Instance instance) const override;
#endif
        [[nodiscard]] WGPUSurface                  createWebGPUSurface(WGPUInstance instance) const override;

        void pollEvents(int timeoutMillis = 0) override;
        void setGamepadRumble(float lowFrequency, float highFrequency, uint32_t durationMs) override;
        void close() override;
        void minimize() override;
        void maximize() override;
        void restore() override;

        [[nodiscard]] SDL_Window* getHandle() const { return m_WindowHandle; }

        static void shutdown();

    private:
        void applyCursor();
        void applyCursorVisibility();
        SDL_Cursor* ensureCursor(CursorType cursor);
        SDL_Cursor* createColorCursor(const CursorImage& cursorImage) const;

        [[nodiscard]] static DriverType translateDriverType();
        [[nodiscard]] static KeyCode    translateKeyCode(int scancode);
        [[nodiscard]] static MouseCode  translateMouseCode(uint8_t button);

        void openGamepad(int instanceId);
        void closeGamepad(int instanceId);

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
        bool        m_Decorated {true};
        bool        m_Visible {true};
        bool        m_ShouldClose {false};
        bool        m_IsMinimized {false};
        // Borderless windows can't use SDL_MaximizeWindow (it fills the whole display, covering the OS
        // task bar). maximize() instead resizes to the display work area and tracks that here so
        // restore() can return to the windowed rect and isMaximized() reports the right state.
        bool        m_PseudoMaximized {false};
        Extent      m_RestoreExtent {};
        Position    m_RestorePosition {};

        SDL_Window*              m_WindowHandle {nullptr};
        // First connected gamepad (single-player). m_GamepadId is its SDL instance id.
        SDL_Gamepad* m_Gamepad {nullptr};
        int          m_GamepadId {0};
        std::array<SDL_Cursor*, static_cast<size_t>(CursorType::eCount)> m_CursorHandles {};
        SDL_Cursor*              m_CustomCursorHandle {nullptr};
        std::optional<CursorImage> m_CustomCursorImage;
        SDL_Cursor*              m_OverrideCursorHandle {nullptr};
        std::optional<CursorImage> m_OverrideCursorImage;
        bool                     m_HasCustomCursor {false};
        bool                     m_HasCursorOverride {false};
        mutable void* m_WebGpuMetalView {nullptr};
#if defined(VULTRA_ENABLE_VULKAN) && VULTRA_ENABLE_VULKAN
        std::vector<const char*> m_VulkanExtensions;
#endif
    };
} // namespace vultra::platform::sdl

#endif
