#include <vultra/core/base/logger.hpp>
#include <vultra/core/os/window.hpp>
#include <vultra/platform/glfw/input.hpp>

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>

#include <stdexcept>
#include <utility>

namespace vultra
{
    Window::Window(const char* title, Extent extent) :
        m_Title(title)
    {
        glfwSetErrorCallback(
            [](int code, const char* text)
            {
                Logger::core().error("[GLFW {}] {}", code, text);
            });
        if (!glfwInit())
        {
            throw std::runtime_error("GLFW initialization failed");
        }
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        m_Window = glfwCreateWindow(int(extent.width), int(extent.height), title, nullptr, nullptr);
        if (!m_Window)
        {
            glfwTerminate();
            throw std::runtime_error("Window creation failed");
        }
        platform::installGlfwInput(m_Window, m_Input);
    }

    Window::Window(GLFWwindow* borrowed) :
        m_Window(borrowed),
        m_Owned(false)
    {
        if (!borrowed)
        {
            throw std::invalid_argument("Cannot wrap a null GLFW window");
        }
        m_Title = glfwGetWindowTitle(borrowed);
    }

    Window::~Window()
    {
        if (m_Owned)
        {
            glfwDestroyWindow(m_Window);
            glfwTerminate();
        }
    }

    void Window::setTitle(const char* title)
    {
        m_Title = title;
        glfwSetWindowTitle(m_Window, (m_Title + m_TitleSuffix).c_str());
    }

    void Window::setTitleSuffix(std::string suffix)
    {
        m_TitleSuffix = std::move(suffix);
        glfwSetWindowTitle(m_Window, (m_Title + m_TitleSuffix).c_str());
    }

    bool Window::poll()
    {
        glfwPollEvents();
        m_Input.advanceFrame();
        return !glfwWindowShouldClose(m_Window);
    }

    Extent Window::framebufferSize() const
    {
        int width  = 0;
        int height = 0;
        glfwGetFramebufferSize(m_Window, &width, &height);
        return {uint32_t(width), uint32_t(height)};
    }

    Extent Window::size() const
    {
        int width  = 0;
        int height = 0;
        glfwGetWindowSize(m_Window, &width, &height);
        return {uint32_t(width), uint32_t(height)};
    }

} // namespace vultra
