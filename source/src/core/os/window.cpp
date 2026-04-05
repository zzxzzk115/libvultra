#include "vultra/core/os/window.hpp"

#include "vultra/core/base/base.hpp"
#include "vultra/core/base/common_context.hpp"

#if defined(__ANDROID__)
#include "vultra/platform/android/android_native_window.hpp"
#elif defined(__EMSCRIPTEN__)
#include "vultra/platform/glfw/glfw_window.hpp"
#else
#include "vultra/platform/glfw/glfw_window.hpp"
#include "vultra/platform/sdl/sdl_window.hpp"
#endif

namespace vultra
{
    namespace os
    {
        Window::Builder& Window::Builder::setTitle(std::string_view title)
        {
            m_Title = title;
            return *this;
        }

        Window::Builder& Window::Builder::setExtent(Extent extent)
        {
            m_Extent = extent;
            return *this;
        }

        Window::Builder& Window::Builder::setPosition(Position position)
        {
            m_Position = position;
            return *this;
        }

        Window::Builder& Window::Builder::setCursorVisibility(bool cursorVisibility)
        {
            m_CursorVisibility = cursorVisibility;
            return *this;
        }

        Window::Builder& Window::Builder::setResizable(bool resizable)
        {
            m_Resizable = resizable;
            return *this;
        }

        Window::Builder& Window::Builder::setFullscreen(bool fullscreen)
        {
            m_Fullscreen = fullscreen;
            return *this;
        }

        Window::Builder& Window::Builder::setPlatform(const PlatformType platformType)
        {
            m_PlatformType = platformType;
            return *this;
        }

        std::shared_ptr<Window> Window::Builder::build() const
        {
#if defined(__ANDROID__)
            VULTRA_CORE_ASSERT(false,
                               "[Window::Builder] Android windows must be created from the Android runtime context.");
            return {};
#elif defined(__EMSCRIPTEN__)
            return std::make_shared<platform::glfw::GLFWWindow>(m_Title, m_Extent, m_Resizable, m_Fullscreen);
#else
            if (m_PlatformType == PlatformType::eGLFW)
            {
                return std::make_shared<platform::glfw::GLFWWindow>(m_Title, m_Extent, m_Resizable, m_Fullscreen);
            }
            return std::make_shared<platform::sdl::SDLWindow>(
                m_Title, m_Extent, m_Position, m_CursorVisibility, m_Resizable, m_Fullscreen);
#endif
        }

        void Window::shutdownPlatform()
        {
#if defined(__ANDROID__)
            platform::android::AndroidNativeWindow::shutdown();
#elif defined(__EMSCRIPTEN__)
            platform::glfw::GLFWWindow::shutdown();
#else
            platform::glfw::GLFWWindow::shutdown();
            platform::sdl::SDLWindow::shutdown();
#endif
        }
    } // namespace os
} // namespace vultra
