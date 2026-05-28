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

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

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

        Window::Builder& Window::Builder::setDecorated(bool decorated)
        {
            m_Decorated = decorated;
            return *this;
        }

        Window::Builder& Window::Builder::setVisible(bool visible)
        {
            m_Visible = visible;
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
            return std::make_shared<platform::glfw::GLFWWindow>(
                m_Title, m_Extent, m_Resizable, m_Fullscreen, m_Decorated, m_Visible);
#else
            if (m_PlatformType == PlatformType::eGLFW)
            {
                return std::make_shared<platform::glfw::GLFWWindow>(
                    m_Title, m_Extent, m_Resizable, m_Fullscreen, m_Decorated, m_Visible);
            }
            return std::make_shared<platform::sdl::SDLWindow>(
                m_Title, m_Extent, m_Position, m_CursorVisibility, m_Resizable, m_Fullscreen, m_Decorated, m_Visible);
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

        std::optional<Window::CursorImage>
        Window::decodeCursorImage(const std::span<const uint8_t> encodedBytes, const int hotX, const int hotY)
        {
            if (encodedBytes.empty())
            {
                return std::nullopt;
            }

            int            width  = 0;
            int            height = 0;
            unsigned char* pixels = stbi_load_from_memory(
                encodedBytes.data(), static_cast<int>(encodedBytes.size()), &width, &height, nullptr, STBI_rgb_alpha);
            if (pixels == nullptr || width <= 0 || height <= 0)
            {
                return std::nullopt;
            }

            CursorImage cursorImage {};
            cursorImage.width  = width;
            cursorImage.height = height;
            cursorImage.hotX   = hotX;
            cursorImage.hotY   = hotY;
            cursorImage.pixels.assign(pixels, pixels + static_cast<size_t>(width * height * 4));
            stbi_image_free(pixels);

            if (!cursorImage.valid())
            {
                return std::nullopt;
            }
            return cursorImage;
        }
    } // namespace os
} // namespace vultra
