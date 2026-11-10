#include <vultra/core/base/logger.hpp>
#include <vultra/platform/glfw/input.hpp>
#include <vultra/platform/window.hpp>

#include <vri/integration/vri_glfw.h>

#include <limits>
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
        Window(Uninitialized {})
    {
        if (auto initialized = initializeOwned(title, extent); !initialized)
            throw std::runtime_error(initialized.error());
    }

    Window::Window(void* borrowed) :
        Window(Uninitialized {})
    {
        if (auto initialized = initializeBorrowed(borrowed); !initialized)
            throw std::invalid_argument(initialized.error());
    }

    std::expected<void, std::string> Window::initializeOwned(const char* title, Extent extent)
    {
        if (!title || extent.empty() || extent.width > uint32_t(std::numeric_limits<int>::max()) ||
            extent.height > uint32_t(std::numeric_limits<int>::max()))
            return std::unexpected("Invalid GLFW window title or extent");
        if (ownedWindows == 0)
        {
            glfwSetErrorCallback(
                [](int code, const char* text)
                {
                    Logger::core().error("[GLFW {}] {}", code, text);
                });
#if defined(__linux__)
            auto requested = platform::requestedWindowSystemExpected();
            if (!requested)
                return std::unexpected(requested.error());
            int windowSystem = GLFW_ANY_PLATFORM;
            if (*requested)
                windowSystem = std::string_view(*requested) == "wayland" ? GLFW_PLATFORM_WAYLAND : GLFW_PLATFORM_X11;
            glfwInitHint(GLFW_PLATFORM, windowSystem);
#endif
            if (!glfwInit())
                return std::unexpected("GLFW initialization failed");
        }
        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
        m_Window = glfwCreateWindow(int(extent.width), int(extent.height), title, nullptr, nullptr);
        // Hints are process-wide; ImGui supplies native viewport sizes and applies its own DPI policy.
        glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_FALSE);
        if (!m_Window)
        {
            if (ownedWindows == 0)
                glfwTerminate();
            return std::unexpected("GLFW window creation failed");
        }
        m_Title = title;
        m_Owned = true;
        ++ownedWindows;
        platform::installGlfwInput(static_cast<GLFWwindow*>(m_Window), m_Input);
        return {};
    }

    std::expected<void, std::string> Window::initializeBorrowed(void* borrowed)
    {
        if (!borrowed)
            return std::unexpected("Cannot wrap a null GLFW window");
        m_Window = borrowed;
        m_Title  = title();
        return {};
    }

    Window::~Window()
    {
        if (m_Owned && m_Window)
        {
            glfwDestroyWindow(static_cast<GLFWwindow*>(m_Window));
            if (--ownedWindows == 0)
                glfwTerminate();
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

    std::expected<Extent, std::string> Window::framebufferSizeExpected() const
    {
        int width  = 0;
        int height = 0;
        glfwGetFramebufferSize(static_cast<GLFWwindow*>(m_Window), &width, &height);
        if (width < 0 || height < 0)
            return std::unexpected("GLFW returned a negative framebuffer extent");
        return Extent {uint32_t(width), uint32_t(height)};
    }

    std::expected<Extent, std::string> Window::sizeExpected() const
    {
        int width  = 0;
        int height = 0;
        glfwGetWindowSize(static_cast<GLFWwindow*>(m_Window), &width, &height);
        if (width < 0 || height < 0)
            return std::unexpected("GLFW returned a negative window extent");
        return Extent {uint32_t(width), uint32_t(height)};
    }

    Extent Window::framebufferSize() const
    {
        auto result = framebufferSizeExpected();
        if (!result)
            throw std::runtime_error(result.error());
        return *result;
    }

    Extent Window::size() const
    {
        auto result = sizeExpected();
        if (!result)
            throw std::runtime_error(result.error());
        return *result;
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
