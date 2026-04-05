#include "vultra/platform/glfw/glfw_window.hpp"

#include "vultra/core/base/common_context.hpp"

#include <GLFW/glfw3.h>
#if defined(__EMSCRIPTEN__)
#include <emscripten/html5.h>
#endif
#include <glfw3webgpu.h>

#include <algorithm>
#include <stdexcept>

namespace vultra::platform::glfw
{
    GLFWWindow::GLFWWindow(std::string_view title, Extent extent, bool resizable, bool fullscreen) :
        m_Title(title), m_Extent(extent), m_Resizable(resizable), m_Fullscreen(fullscreen)
    {
        if (!glfwInit())
        {
            throw std::runtime_error("Failed to initialize GLFW");
        }

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, m_Resizable ? GLFW_TRUE : GLFW_FALSE);
        m_WindowHandle = glfwCreateWindow(std::max(1, m_Extent.x), std::max(1, m_Extent.y), m_Title.c_str(), nullptr, nullptr);
        if (!m_WindowHandle)
        {
            throw std::runtime_error("Failed to create GLFW window");
        }

        glfwGetFramebufferSize(m_WindowHandle, &m_FrameBufferExtent.x, &m_FrameBufferExtent.y);
        glfwGetWindowPos(m_WindowHandle, &m_Position.x, &m_Position.y);

        glfwSetWindowUserPointer(m_WindowHandle, this);
        glfwSetWindowCloseCallback(m_WindowHandle, &GLFWWindow::onWindowClose);
        glfwSetWindowSizeCallback(m_WindowHandle, &GLFWWindow::onWindowSize);
        glfwSetWindowPosCallback(m_WindowHandle, &GLFWWindow::onWindowPos);
        glfwSetFramebufferSizeCallback(m_WindowHandle, &GLFWWindow::onFramebufferSize);
        glfwSetKeyCallback(m_WindowHandle, &GLFWWindow::onKey);
        glfwSetMouseButtonCallback(m_WindowHandle, &GLFWWindow::onMouseButton);
        glfwSetCursorPosCallback(m_WindowHandle, &GLFWWindow::onCursorPos);
        glfwSetScrollCallback(m_WindowHandle, &GLFWWindow::onScroll);

#if defined(VULTRA_ENABLE_VULKAN) && VULTRA_ENABLE_VULKAN
        uint32_t count = 0;
        const auto* extensions = glfwGetRequiredInstanceExtensions(&count);
        if (extensions != nullptr && count > 0u)
        {
            m_VulkanExtensions.assign(extensions, extensions + count);
        }
#endif
    }

    GLFWWindow::~GLFWWindow()
    {
        if (m_WindowHandle)
        {
            glfwDestroyWindow(m_WindowHandle);
            m_WindowHandle = nullptr;
        }
    }

    os::Window& GLFWWindow::setTitle(std::string_view title)
    {
        m_Title = title;
        if (m_WindowHandle)
        {
            glfwSetWindowTitle(m_WindowHandle, m_Title.c_str());
        }
        return *this;
    }

    os::Window& GLFWWindow::setExtent(Extent extent)
    {
        m_Extent = extent;
        if (m_WindowHandle)
        {
            glfwSetWindowSize(m_WindowHandle, std::max(1, extent.x), std::max(1, extent.y));
        }
        return *this;
    }

    os::Window& GLFWWindow::setPosition(Position position)
    {
        m_Position = position;
        if (m_WindowHandle)
        {
            glfwSetWindowPos(m_WindowHandle, position.x, position.y);
        }
        return *this;
    }

    os::Window& GLFWWindow::setCursor(CursorType cursor)
    {
        m_Cursor = cursor;
        return *this;
    }

    os::Window& GLFWWindow::setCursorVisibility(bool cursorVisibility)
    {
        m_CursorVisibility = cursorVisibility;
        if (m_WindowHandle)
        {
            glfwSetInputMode(m_WindowHandle, GLFW_CURSOR, cursorVisibility ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
        }
        return *this;
    }

    os::Window& GLFWWindow::setMouseRelativeMode(bool mouseRelativeMode)
    {
        m_MouseRelativeMode = mouseRelativeMode;
        return setCursorVisibility(!mouseRelativeMode);
    }

    os::Window& GLFWWindow::setResizable(bool resizable)
    {
        m_Resizable = resizable;
        return *this;
    }

    os::Window& GLFWWindow::setFullscreen(bool fullscreen)
    {
        m_Fullscreen = fullscreen;
        return *this;
    }

    rhi::Rect2D GLFWWindow::getContentArea() const
    {
        return rhi::Rect2D {
            .offset = {0, 0},
            .extent = {static_cast<uint32_t>(std::max(m_FrameBufferExtent.x, 0)),
                       static_cast<uint32_t>(std::max(m_FrameBufferExtent.y, 0))}
        };
    }

    float GLFWWindow::getDisplayScale() const
    {
#if defined(__EMSCRIPTEN__)
        return static_cast<float>(emscripten_get_device_pixel_ratio());
#else
        if (!m_WindowHandle)
        {
            return 1.0f;
        }
        float xscale = 1.0f;
        float yscale = 1.0f;
        glfwGetWindowContentScale(m_WindowHandle, &xscale, &yscale);
        return std::max(xscale, yscale);
#endif
    }

#if defined(VULTRA_ENABLE_VULKAN) && VULTRA_ENABLE_VULKAN
    std::span<const char* const> GLFWWindow::getRequiredVulkanInstanceExtensions() const
    {
        return {m_VulkanExtensions.data(), m_VulkanExtensions.size()};
    }

    vk::SurfaceKHR GLFWWindow::createVulkanSurface(vk::Instance instance) const
    {
        VkSurfaceKHR surface {VK_NULL_HANDLE};
        if (!m_WindowHandle || !instance || glfwCreateWindowSurface(instance, m_WindowHandle, nullptr, &surface) !=
                                                  VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create GLFW Vulkan surface");
        }
        return vk::SurfaceKHR {surface};
    }
#endif

    WGPUSurface GLFWWindow::createWebGPUSurface(const WGPUInstance instance) const
    {
        if (instance == nullptr || m_WindowHandle == nullptr)
        {
            return nullptr;
        }
        return glfwGetWGPUSurface(instance, m_WindowHandle);
    }

    void GLFWWindow::pollEvents(int)
    {
        glfwPollEvents();
        if (m_WindowHandle)
        {
            glfwGetFramebufferSize(m_WindowHandle, &m_FrameBufferExtent.x, &m_FrameBufferExtent.y);
            m_ShouldClose = glfwWindowShouldClose(m_WindowHandle) != 0;
        }
    }

    void GLFWWindow::close()
    {
        m_ShouldClose = true;
        if (m_WindowHandle)
        {
            glfwSetWindowShouldClose(m_WindowHandle, GLFW_TRUE);
        }
    }

    void GLFWWindow::shutdown()
    {
        glfwTerminate();
    }

    KeyCode GLFWWindow::translateKeyCode(const int key)
    {
        switch (key)
        {
            case GLFW_KEY_A:
                return KeyCode::eA;
            case GLFW_KEY_B:
                return KeyCode::eB;
            case GLFW_KEY_C:
                return KeyCode::eC;
            case GLFW_KEY_D:
                return KeyCode::eD;
            case GLFW_KEY_E:
                return KeyCode::eE;
            case GLFW_KEY_F:
                return KeyCode::eF;
            case GLFW_KEY_G:
                return KeyCode::eG;
            case GLFW_KEY_H:
                return KeyCode::eH;
            case GLFW_KEY_I:
                return KeyCode::eI;
            case GLFW_KEY_J:
                return KeyCode::eJ;
            case GLFW_KEY_K:
                return KeyCode::eK;
            case GLFW_KEY_L:
                return KeyCode::eL;
            case GLFW_KEY_M:
                return KeyCode::eM;
            case GLFW_KEY_N:
                return KeyCode::eN;
            case GLFW_KEY_O:
                return KeyCode::eO;
            case GLFW_KEY_P:
                return KeyCode::eP;
            case GLFW_KEY_Q:
                return KeyCode::eQ;
            case GLFW_KEY_R:
                return KeyCode::eR;
            case GLFW_KEY_S:
                return KeyCode::eS;
            case GLFW_KEY_T:
                return KeyCode::eT;
            case GLFW_KEY_U:
                return KeyCode::eU;
            case GLFW_KEY_V:
                return KeyCode::eV;
            case GLFW_KEY_W:
                return KeyCode::eW;
            case GLFW_KEY_X:
                return KeyCode::eX;
            case GLFW_KEY_Y:
                return KeyCode::eY;
            case GLFW_KEY_Z:
                return KeyCode::eZ;
            case GLFW_KEY_0:
                return KeyCode::eNum0;
            case GLFW_KEY_1:
                return KeyCode::eNum1;
            case GLFW_KEY_2:
                return KeyCode::eNum2;
            case GLFW_KEY_3:
                return KeyCode::eNum3;
            case GLFW_KEY_4:
                return KeyCode::eNum4;
            case GLFW_KEY_5:
                return KeyCode::eNum5;
            case GLFW_KEY_6:
                return KeyCode::eNum6;
            case GLFW_KEY_7:
                return KeyCode::eNum7;
            case GLFW_KEY_8:
                return KeyCode::eNum8;
            case GLFW_KEY_9:
                return KeyCode::eNum9;
            case GLFW_KEY_ENTER:
                return KeyCode::eReturn;
            case GLFW_KEY_ESCAPE:
                return KeyCode::eEscape;
            case GLFW_KEY_BACKSPACE:
                return KeyCode::eBackspace;
            case GLFW_KEY_TAB:
                return KeyCode::eTab;
            case GLFW_KEY_SPACE:
                return KeyCode::eSpace;
            case GLFW_KEY_MINUS:
                return KeyCode::eMinus;
            case GLFW_KEY_EQUAL:
                return KeyCode::eEquals;
            case GLFW_KEY_LEFT_BRACKET:
                return KeyCode::eLeftBracket;
            case GLFW_KEY_RIGHT_BRACKET:
                return KeyCode::eRightBracket;
            case GLFW_KEY_BACKSLASH:
                return KeyCode::eBackslash;
            case GLFW_KEY_SEMICOLON:
                return KeyCode::eSemicolon;
            case GLFW_KEY_APOSTROPHE:
                return KeyCode::eApostrophe;
            case GLFW_KEY_GRAVE_ACCENT:
                return KeyCode::eGrave;
            case GLFW_KEY_COMMA:
                return KeyCode::eComma;
            case GLFW_KEY_PERIOD:
                return KeyCode::ePeriod;
            case GLFW_KEY_SLASH:
                return KeyCode::eSlash;
            case GLFW_KEY_LEFT_SHIFT:
                return KeyCode::eLShift;
            case GLFW_KEY_RIGHT_SHIFT:
                return KeyCode::eRShift;
            case GLFW_KEY_LEFT_CONTROL:
                return KeyCode::eLCtrl;
            case GLFW_KEY_RIGHT_CONTROL:
                return KeyCode::eRCtrl;
            case GLFW_KEY_LEFT_ALT:
                return KeyCode::eLAlt;
            case GLFW_KEY_RIGHT_ALT:
                return KeyCode::eRAlt;
            case GLFW_KEY_LEFT:
                return KeyCode::eLeft;
            case GLFW_KEY_RIGHT:
                return KeyCode::eRight;
            case GLFW_KEY_UP:
                return KeyCode::eUp;
            case GLFW_KEY_DOWN:
                return KeyCode::eDown;
            default:
                return KeyCode::eUnknown;
        }
    }

    MouseCode GLFWWindow::translateMouseCode(const int button)
    {
        switch (button)
        {
            case GLFW_MOUSE_BUTTON_LEFT:
                return MouseCode::eLeft;
            case GLFW_MOUSE_BUTTON_MIDDLE:
                return MouseCode::eMiddle;
            case GLFW_MOUSE_BUTTON_RIGHT:
                return MouseCode::eRight;
            case GLFW_MOUSE_BUTTON_4:
                return MouseCode::eX1;
            case GLFW_MOUSE_BUTTON_5:
                return MouseCode::eX2;
            default:
                return MouseCode::eLeft;
        }
    }

    GLFWWindow* GLFWWindow::fromHandle(GLFWwindow* windowHandle)
    {
        if (!windowHandle)
        {
            return nullptr;
        }
        return static_cast<GLFWWindow*>(glfwGetWindowUserPointer(windowHandle));
    }

    void GLFWWindow::onWindowClose(GLFWwindow* windowHandle)
    {
        auto* self = fromHandle(windowHandle);
        if (!self)
        {
            return;
        }
        self->m_ShouldClose = true;
        os::GeneralWindowEvent generalEvent {};
        generalEvent.type = event::WindowEventType::eCloseRequested;
        self->emitEvent(generalEvent);
    }

    void GLFWWindow::onWindowSize(GLFWwindow* windowHandle, int width, int height)
    {
        auto* self = fromHandle(windowHandle);
        if (!self)
        {
            return;
        }
        self->m_Extent = {width, height};
        os::GeneralWindowEvent generalEvent {};
        generalEvent.type = event::WindowEventType::eResized;
        self->emitEvent(generalEvent);
    }

    void GLFWWindow::onWindowPos(GLFWwindow* windowHandle, int x, int y)
    {
        auto* self = fromHandle(windowHandle);
        if (!self)
        {
            return;
        }
        self->m_Position = {x, y};
        os::GeneralWindowEvent generalEvent {};
        generalEvent.type = event::WindowEventType::eMoved;
        self->emitEvent(generalEvent);
    }

    void GLFWWindow::onFramebufferSize(GLFWwindow* windowHandle, int width, int height)
    {
        auto* self = fromHandle(windowHandle);
        if (!self)
        {
            return;
        }
        self->m_FrameBufferExtent = {width, height};
    }

    void GLFWWindow::onKey(GLFWwindow* windowHandle, int key, int, int action, int)
    {
        auto* self = fromHandle(windowHandle);
        if (!self)
        {
            return;
        }

        os::GeneralWindowEvent generalEvent {};
        if (action == GLFW_PRESS || action == GLFW_REPEAT)
        {
            generalEvent.type = event::WindowEventType::eKeyDown;
            generalEvent.key  = event::KeyEvent {
                .key    = translateKeyCode(key),
                .repeat = action == GLFW_REPEAT,
            };
            self->emitEvent(generalEvent);
        }
        else if (action == GLFW_RELEASE)
        {
            generalEvent.type = event::WindowEventType::eKeyUp;
            generalEvent.key  = event::KeyEvent {
                .key    = translateKeyCode(key),
                .repeat = false,
            };
            self->emitEvent(generalEvent);
        }
    }

    void GLFWWindow::onMouseButton(GLFWwindow* windowHandle, int button, int action, int)
    {
        auto* self = fromHandle(windowHandle);
        if (!self)
        {
            return;
        }

        os::GeneralWindowEvent generalEvent {};
        generalEvent.mouseButton = event::MouseButtonEvent {
            .button = translateMouseCode(button),
            .clicks = 1,
        };

        if (action == GLFW_PRESS)
        {
            generalEvent.type = event::WindowEventType::eMouseButtonDown;
            self->emitEvent(generalEvent);
        }
        else if (action == GLFW_RELEASE)
        {
            generalEvent.type = event::WindowEventType::eMouseButtonUp;
            self->emitEvent(generalEvent);
        }
    }

    void GLFWWindow::onCursorPos(GLFWwindow* windowHandle, double xpos, double ypos)
    {
        auto* self = fromHandle(windowHandle);
        if (!self)
        {
            return;
        }

        const glm::vec2 current {static_cast<float>(xpos), static_cast<float>(ypos)};
        const glm::vec2 delta =
            self->m_HasLastCursorPosition ? (current - self->m_LastCursorPosition) : glm::vec2 {0.0f};
        self->m_LastCursorPosition    = current;
        self->m_HasLastCursorPosition = true;

        os::GeneralWindowEvent generalEvent {};
        generalEvent.type = event::WindowEventType::eMouseMotion;
        generalEvent.mouseMotion = event::MouseMotionEvent {
            .position = current,
            .delta    = delta,
        };
        self->emitEvent(generalEvent);
    }

    void GLFWWindow::onScroll(GLFWwindow* windowHandle, double xoffset, double yoffset)
    {
        auto* self = fromHandle(windowHandle);
        if (!self)
        {
            return;
        }

        os::GeneralWindowEvent generalEvent {};
        generalEvent.type = event::WindowEventType::eMouseWheel;
        generalEvent.mouseWheel = event::MouseWheelEvent {
            .delta = {static_cast<float>(xoffset), static_cast<float>(yoffset)},
        };
        self->emitEvent(generalEvent);
    }
} // namespace vultra::platform::glfw
