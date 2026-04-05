#pragma once

#include "vultra/core/os/window.hpp"

struct GLFWwindow;

namespace vultra::platform::glfw
{
    class GLFWWindow final : public os::Window
    {
    public:
        GLFWWindow(std::string_view title, Extent extent, bool resizable, bool fullscreen);
        ~GLFWWindow() override;

        [[nodiscard]] PlatformType platformType() const override { return PlatformType::eGLFW; }
        [[nodiscard]] DriverType   driverType() const override { return DriverType::eUnknown; }

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
        [[nodiscard]] rhi::Rect2D      getContentArea() const override;
        [[nodiscard]] Position         getPosition() const override { return m_Position; }
        [[nodiscard]] CursorType       getCursor() const override { return m_Cursor; }
        [[nodiscard]] bool             getCursorVisibility() const override { return m_CursorVisibility; }
        [[nodiscard]] bool             getMouseRelativeMode() const override { return m_MouseRelativeMode; }
        [[nodiscard]] bool             isResizable() const override { return m_Resizable; }
        [[nodiscard]] bool             isFullscreen() const override { return m_Fullscreen; }
        [[nodiscard]] float            getDisplayScale() const override;
        [[nodiscard]] bool             shouldClose() const override { return m_ShouldClose; }
        [[nodiscard]] bool             isMinimized() const override { return false; }
        [[nodiscard]] bool             isReady() const override { return m_WindowHandle != nullptr; }
        [[nodiscard]] GLFWwindow*      getHandle() const { return m_WindowHandle; }

#if defined(VULTRA_ENABLE_VULKAN) && VULTRA_ENABLE_VULKAN
        [[nodiscard]] std::span<const char* const> getRequiredVulkanInstanceExtensions() const override;
        [[nodiscard]] vk::SurfaceKHR               createVulkanSurface(vk::Instance instance) const override;
#endif
        [[nodiscard]] WGPUSurface createWebGPUSurface(WGPUInstance instance) const override;

        void pollEvents(int timeoutMillis = 0) override;
        void close() override;

        static void shutdown();

    private:
        [[nodiscard]] static KeyCode   translateKeyCode(int key);
        [[nodiscard]] static MouseCode translateMouseCode(int button);

        static GLFWWindow* fromHandle(GLFWwindow* windowHandle);
        static void onWindowClose(GLFWwindow* windowHandle);
        static void onWindowSize(GLFWwindow* windowHandle, int width, int height);
        static void onWindowPos(GLFWwindow* windowHandle, int x, int y);
        static void onFramebufferSize(GLFWwindow* windowHandle, int width, int height);
        static void onKey(GLFWwindow* windowHandle, int key, int scancode, int action, int mods);
        static void onMouseButton(GLFWwindow* windowHandle, int button, int action, int mods);
        static void onCursorPos(GLFWwindow* windowHandle, double xpos, double ypos);
        static void onScroll(GLFWwindow* windowHandle, double xoffset, double yoffset);

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
        glm::vec2   m_LastCursorPosition {};
        bool        m_HasLastCursorPosition {false};

        GLFWwindow* m_WindowHandle {nullptr};
#if defined(VULTRA_ENABLE_VULKAN) && VULTRA_ENABLE_VULKAN
        std::vector<const char*> m_VulkanExtensions;
#endif
    };
} // namespace vultra::platform::glfw
