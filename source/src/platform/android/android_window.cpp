#include "vultra/platform/android/android_window.hpp"

#if defined(__ANDROID__)

#include <android_native_app_glue.h>
#include <vulkan/vulkan_android.h>

#include "vultra/core/base/common_context.hpp"

namespace vultra::platform::android
{

    AndroidWindow::AndroidWindow(android_app& app) : m_App(&app)
    {
        m_NativeWindow = m_App->window;
        m_ShouldClose  = m_App->destroyRequested != 0;
    }

    AndroidWindow::AndroidWindow(ANativeWindow* nativeWindow, const int* destroyRequested)
        : m_DestroyRequested(destroyRequested), m_NativeWindow(nativeWindow)
    {
        m_ShouldClose = m_DestroyRequested != nullptr && *m_DestroyRequested != 0;
        updateWindowState();
    }

    bool AndroidWindow::pollEvents(const int timeoutMillis)
    {
        if (m_App == nullptr)
        {
            if (timeoutMillis > 0)
            {
                (void)ALooper_pollOnce(timeoutMillis, nullptr, nullptr, nullptr);
            }
            updateWindowState();
            return !m_ShouldClose;
        }

        int                  events = 0;
        android_poll_source* source = nullptr;

        while (ALooper_pollOnce(timeoutMillis, nullptr, &events, reinterpret_cast<void**>(&source)) >= 0)
        {
            if (source != nullptr)
            {
                source->process(m_App, source);
            }

            if (m_App->destroyRequested != 0)
            {
                m_ShouldClose = true;
                break;
            }

            if (timeoutMillis != 0)
            {
                break;
            }
        }

        updateWindowState();
        return !m_ShouldClose;
    }

    bool AndroidWindow::waitForWindow(const int timeoutMillis)
    {
        while (!m_ShouldClose && !hasWindow())
        {
            (void)pollEvents(timeoutMillis);
        }

        return hasWindow();
    }

    bool AndroidWindow::hasWindow() const { return m_NativeWindow != nullptr; }

    ANativeWindow* AndroidWindow::nativeWindow() const { return m_NativeWindow; }

    AndroidWindow::Extent AndroidWindow::extent() const { return m_Extent; }

    AndroidWindow::Extent AndroidWindow::getFrameBufferExtent() const { return m_Extent; }

    bool AndroidWindow::shouldClose() const { return m_ShouldClose; }

    bool AndroidWindow::isReady() const { return hasWindow(); }

    vk::SurfaceKHR AndroidWindow::createVulkanSurface(const vk::Instance instance) const
    {
        VkAndroidSurfaceCreateInfoKHR createInfo {};
        createInfo.sType  = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
        createInfo.window = m_NativeWindow;

        VkSurfaceKHR surface = VK_NULL_HANDLE;
        const auto   result =
            vkCreateAndroidSurfaceKHR(static_cast<VkInstance>(instance), &createInfo, nullptr, &surface);
        if (result != VK_SUCCESS)
        {
            VULTRA_CORE_ERROR("[AndroidWindow] Failed to create Android Vulkan surface, result={}",
                              static_cast<int>(result));
            throw std::runtime_error("Failed to create Android Vulkan surface");
        }

        return vk::SurfaceKHR {surface};
    }

    void AndroidWindow::updateWindowState()
    {
        if (m_App != nullptr)
        {
            m_NativeWindow = m_App->window;
            m_ShouldClose  = m_App->destroyRequested != 0;
        }
        else
        {
            m_ShouldClose = m_DestroyRequested != nullptr && *m_DestroyRequested != 0;
        }
        if (m_NativeWindow != nullptr)
        {
            m_Extent = {ANativeWindow_getWidth(m_NativeWindow), ANativeWindow_getHeight(m_NativeWindow)};
        }
        else
        {
            m_Extent = {};
        }
    }
} // namespace vultra::platform::android

#endif
