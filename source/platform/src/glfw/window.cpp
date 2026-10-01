#include <vultra/core/base/logger.hpp>
#include <vultra/platform/glfw/input.hpp>
#include <vultra/platform/window.hpp>

#include <vri/integration/vri_glfw.h>

#include <stdexcept>
#include <string_view>

namespace vultra
{
    namespace
    {
        // GLFW has one process-wide runtime. Borrowed ImGui windows do not retain it.
        uint32_t ownedWindows = 0;
    } // namespace

    Window::Window(const char* title, Extent extent) :
        m_Title(title)
    {
        if (ownedWindows == 0)
        {
            glfwSetErrorCallback(
                [](int code, const char* text)
                {
                    Logger::core().error("[GLFW {}] {}", code, text);
                });
#if defined(__linux__)
            int windowSystem = GLFW_ANY_PLATFORM;
            if (const auto* requested = platform::requestedWindowSystem())
            {
                windowSystem = std::string_view(requested) == "wayland" ? GLFW_PLATFORM_WAYLAND : GLFW_PLATFORM_X11;
            }
            glfwInitHint(GLFW_PLATFORM, windowSystem);
#endif
            if (!glfwInit())
            {
                throw std::runtime_error("GLFW initialization failed");
            }
        }
        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        m_Window = glfwCreateWindow(int(extent.width), int(extent.height), title, nullptr, nullptr);
        if (!m_Window)
        {
            if (ownedWindows == 0)
            {
                glfwTerminate();
            }
            throw std::runtime_error("GLFW window creation failed");
        }
        ++ownedWindows;
        platform::installGlfwInput(static_cast<GLFWwindow*>(m_Window), m_Input);
    }

    Window::Window(void* borrowed) :
        m_Window(borrowed),
        m_Owned(false)
    {
        if (!borrowed)
        {
            throw std::invalid_argument("Cannot wrap a null GLFW window");
        }
        m_Title = title();
    }

    Window::~Window()
    {
        if (m_Owned)
        {
            glfwDestroyWindow(static_cast<GLFWwindow*>(m_Window));
            if (--ownedWindows == 0)
            {
                glfwTerminate();
            }
        }
    }

    void Window::updateTitle()
    {
        glfwSetWindowTitle(static_cast<GLFWwindow*>(m_Window), (m_Title + m_TitleSuffix).c_str());
    }

    std::string Window::title() const
    {
        return glfwGetWindowTitle(static_cast<GLFWwindow*>(m_Window));
    }

    bool Window::poll()
    {
        glfwPollEvents();
        m_Input.advanceFrame();
        return !glfwWindowShouldClose(static_cast<GLFWwindow*>(m_Window));
    }

    void Window::waitEvents(double seconds)
    {
        glfwWaitEventsTimeout(seconds);
    }

    Extent Window::framebufferSize() const
    {
        int width  = 0;
        int height = 0;
        glfwGetFramebufferSize(static_cast<GLFWwindow*>(m_Window), &width, &height);
        return {uint32_t(width), uint32_t(height)};
    }

    Extent Window::size() const
    {
        int width  = 0;
        int height = 0;
        glfwGetWindowSize(static_cast<GLFWwindow*>(m_Window), &width, &height);
        return {uint32_t(width), uint32_t(height)};
    }

    void Window::setTextInputEnabled(bool)
    {
        // GLFW character callbacks are active without an explicit text-input session.
    }

    void Window::setSize(Extent extent)
    {
        glfwSetWindowSize(static_cast<GLFWwindow*>(m_Window), int(extent.width), int(extent.height));
    }

    void Window::minimize()
    {
        glfwIconifyWindow(static_cast<GLFWwindow*>(m_Window));
    }

    void Window::restore()
    {
        glfwRestoreWindow(static_cast<GLFWwindow*>(m_Window));
    }

    bool Window::minimized() const
    {
        return glfwGetWindowAttrib(static_cast<GLFWwindow*>(m_Window), GLFW_ICONIFIED) != 0;
    }

    namespace platform
    {
        VriWindowHandle nativeWindow(const Window& window)
        {
            return vriWindowHandleFromGLFW(static_cast<GLFWwindow*>(window.handle()));
        }
    } // namespace platform
} // namespace vultra
