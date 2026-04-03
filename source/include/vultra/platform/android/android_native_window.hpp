#pragma once

#if defined(__ANDROID__)

#include "vultra/core/os/window.hpp"

#include <algorithm>
#include <android/native_window.h>

struct android_app;
struct GameActivityKeyEvent;
struct GameActivityMotionEvent;

namespace vultra::platform::android
{
    class AndroidNativeWindow final : public os::Window
    {
    public:
        explicit AndroidNativeWindow(android_app& app);
        AndroidNativeWindow(ANativeWindow* nativeWindow, const int* destroyRequested = nullptr);
        ~AndroidNativeWindow() override = default;

        [[nodiscard]] PlatformType platformType() const override { return PlatformType::eAndroidNativeWindow; }
        [[nodiscard]] DriverType   driverType() const override { return DriverType::eAndroid; }

        Window& setTitle(std::string_view title) override;
        Window& setExtent(Extent extent) override;
        Window& setPosition(Position position) override;
        Window& setCursor(CursorType cursor) override;
        Window& setCursorVisibility(bool cursorVisibility) override;
        Window& setMouseRelativeMode(bool mouseRelativeMode) override;
        Window& setResizable(bool resizable) override;
        Window& setFullscreen(bool fullscreen) override;

        [[nodiscard]] std::string_view getTitle() const override { return m_Title; }
        [[nodiscard]] Extent           getExtent() const override { return m_ContentExtent; }
        [[nodiscard]] Extent           getFrameBufferExtent() const override { return m_Extent; }
        [[nodiscard]] rhi::Rect2D      getContentArea() const override
        {
            return rhi::Rect2D {.offset = {m_ContentOffset.x, m_ContentOffset.y},
                                .extent = {static_cast<uint32_t>(std::max(m_ContentExtent.x, 0)),
                                           static_cast<uint32_t>(std::max(m_ContentExtent.y, 0))}};
        }
        [[nodiscard]] Position   getPosition() const override { return {}; }
        [[nodiscard]] CursorType getCursor() const override { return CursorType::eArrow; }
        [[nodiscard]] bool       getCursorVisibility() const override { return true; }
        [[nodiscard]] bool       getMouseRelativeMode() const override { return false; }
        [[nodiscard]] bool       isResizable() const override { return false; }
        [[nodiscard]] bool       isFullscreen() const override { return true; }
        [[nodiscard]] float      getDisplayScale() const override { return m_DisplayScale; }
        [[nodiscard]] bool       shouldClose() const override { return m_ShouldClose; }
        [[nodiscard]] bool       isMinimized() const override { return false; }
        [[nodiscard]] bool       isReady() const override;

        [[nodiscard]] std::span<const char* const> getRequiredVulkanInstanceExtensions() const override;
        [[nodiscard]] vk::SurfaceKHR               createVulkanSurface(vk::Instance instance) const override;
        [[nodiscard]] WGPUSurface                  createWebGPUSurface(WGPUInstance instance) const override;

        void pollEvents(int timeoutMillis) override;
        void close() override;

        [[nodiscard]] ANativeWindow* nativeWindow() const { return m_NativeWindow; }

        static void shutdown();

    private:
        void                         processInputBuffers();
        void                         processKeyEvent(const GameActivityKeyEvent& event);
        void                         processMotionEvent(const GameActivityMotionEvent& event);
        [[nodiscard]] static KeyCode translateKeyCode(int32_t keyCode);
        void                         updateWindowState();

    private:
        android_app*                 m_App {nullptr};
        const int*                   m_DestroyRequested {nullptr};
        ANativeWindow*               m_NativeWindow {nullptr};
        Extent                       m_Extent {};
        Position                     m_ContentOffset {};
        Extent                       m_ContentExtent {};
        float                        m_DisplayScale {1.0f};
        glm::vec2                    m_LastPointerPosition {};
        std::string                  m_Title;
        bool                         m_ShouldClose {false};
        static constexpr const char* s_k_VulkanExtensions[2] = {VK_KHR_SURFACE_EXTENSION_NAME,
                                                                "VK_KHR_android_surface"};
    };
} // namespace vultra::platform::android

#endif
