#pragma once

#if defined(__ANDROID__)

#include <android/native_window.h>
#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>

struct android_app;

namespace vultra::platform::android
{
    class AndroidWindow
    {
    public:
        using Extent = glm::ivec2;

        explicit AndroidWindow(android_app& app);
        explicit AndroidWindow(ANativeWindow* nativeWindow, const int* destroyRequested = nullptr);

        AndroidWindow(const AndroidWindow&) = delete;
        AndroidWindow(AndroidWindow&&)      = delete;
        ~AndroidWindow()                    = default;

        AndroidWindow& operator=(const AndroidWindow&) = delete;
        AndroidWindow& operator=(AndroidWindow&&) = delete;

        [[nodiscard]] bool pollEvents(int timeoutMillis = 0);
        [[nodiscard]] bool waitForWindow(int timeoutMillis = 0);

        [[nodiscard]] bool           hasWindow() const;
        [[nodiscard]] ANativeWindow* nativeWindow() const;
        [[nodiscard]] Extent         extent() const;
        [[nodiscard]] Extent         getFrameBufferExtent() const;
        [[nodiscard]] bool           shouldClose() const;
        [[nodiscard]] bool           isReady() const;
        [[nodiscard]] vk::SurfaceKHR createVulkanSurface(vk::Instance instance) const;

    private:
        void        updateWindowState();

    private:
        android_app*    m_App {nullptr};
        const int*      m_DestroyRequested {nullptr};
        ANativeWindow*  m_NativeWindow {nullptr};
        Extent          m_Extent {};
        bool            m_ShouldClose {false};
    };
} // namespace vultra::platform::android

#endif
