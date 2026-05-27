#include "vultra/platform/glfw/glfw_window.hpp"

#include "vultra/core/base/common_context.hpp"

#include <GLFW/glfw3.h>
#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#endif
#include <glfw3webgpu.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace vultra::platform::glfw
{
#if defined(__EMSCRIPTEN__)
    // clang-format off
    // NOLINTBEGIN
    EM_JS(void, setDocumentAppTitle, (const char* title), {
        const nextTitle = UTF8ToString(title || 0) || "vultra";
        if (typeof Module !== "undefined" && typeof Module.setAppName === "function")
        {
            Module.setAppName(nextTitle);
            return;
        }

        document.title = nextTitle;
        const appTitle = document.getElementById("app-title");
        if (appTitle)
        {
            appTitle.textContent = nextTitle + " / web runtime";
        }
    });

    EM_JS(void, setCanvasCursorStyle, (int cursorType, int visible), {
        const canvas = document.getElementById("canvas");
        if (!canvas)
            return;
        if (!visible)
        {
            canvas.style.cursor = "none";
            return;
        }
        switch (cursorType)
        {
            case 1:
                canvas.style.cursor = "url('https://cdn.jsdelivr.net/npm/@mdi/svg@7.4.47/svg/orbit.svg') 12 12, alias";
                break;
            case 2:
                canvas.style.cursor =
                    "url('https://cdn.jsdelivr.net/npm/@mdi/svg@7.4.47/svg/hand-back-right-outline.svg') 12 10, grabbing";
                break;
            case 3:
                canvas.style.cursor =
                    "url('https://cdn.jsdelivr.net/npm/@mdi/svg@7.4.47/svg/eye-outline.svg') 12 12, pointer";
                break;
            default:
                canvas.style.cursor = "default";
                break;
        }
    });

    EM_JS(void,
          setCanvasCustomCursor,
          (const uint8_t* pixels, int width, int height, int hotX, int hotY, int visible),
          {
              const canvas = document.getElementById("canvas");
              if (!canvas)
                  return;
              if (!visible)
              {
                  canvas.style.cursor = "none";
                  return;
              }

              const size       = width * height * 4;
              const bytes      = HEAPU8.slice(pixels, pixels + size);
              const offscreen  = document.createElement("canvas");
              offscreen.width  = width;
              offscreen.height = height;
              const ctx        = offscreen.getContext("2d");
              if (!ctx)
              {
                  canvas.style.cursor = "default";
                  return;
              }

              const imageData = new ImageData(new Uint8ClampedArray(bytes.buffer), width, height);
              ctx.putImageData(imageData, 0, 0);
              canvas.style.cursor = `url(${offscreen.toDataURL("image/png")}) ${hotX} ${hotY}, auto`;
          });
    // NOLINTEND
    // clang-format on
#endif

    namespace
    {
        bool cursorImagesEqual(const os::Window::CursorImage& lhs, const os::Window::CursorImage& rhs)
        {
            return lhs.width == rhs.width && lhs.height == rhs.height && lhs.hotX == rhs.hotX && lhs.hotY == rhs.hotY &&
                   lhs.pixels == rhs.pixels;
        }

        int toGLFWSystemCursor(const os::Window::CursorType cursor)
        {
            switch (cursor)
            {
                case os::Window::CursorType::eGrab:
                    return GLFW_HAND_CURSOR;
                case os::Window::CursorType::eLook:
                    return GLFW_CROSSHAIR_CURSOR;
                case os::Window::CursorType::eZoomIn:
                case os::Window::CursorType::eZoomOut:
                    return GLFW_ARROW_CURSOR;
                case os::Window::CursorType::eOrbit:
                    return GLFW_HRESIZE_CURSOR;
                case os::Window::CursorType::eTextInput:
                    return GLFW_IBEAM_CURSOR;
                case os::Window::CursorType::eResizeNS:
                    return GLFW_VRESIZE_CURSOR;
                case os::Window::CursorType::eResizeEW:
                    return GLFW_HRESIZE_CURSOR;
                case os::Window::CursorType::eResizeNESW:
                    return GLFW_CROSSHAIR_CURSOR;
                case os::Window::CursorType::eResizeNWSE:
                    return GLFW_CROSSHAIR_CURSOR;
                case os::Window::CursorType::eHand:
                    return GLFW_HAND_CURSOR;
                case os::Window::CursorType::eNotAllowed:
                    return GLFW_CROSSHAIR_CURSOR;
                case os::Window::CursorType::eArrow:
                case os::Window::CursorType::eCount:
                default:
                    return GLFW_ARROW_CURSOR;
            }
        }
    } // namespace

    GLFWWindow::GLFWWindow(std::string_view title,
                           Extent           extent,
                           bool             resizable,
                           bool             fullscreen,
                           bool             decorated,
                           bool             visible) :
        m_Title(title), m_Extent(extent), m_Resizable(resizable), m_Fullscreen(fullscreen), m_Decorated(decorated),
        m_Visible(visible)
    {
        if (!glfwInit())
        {
            throw std::runtime_error("Failed to initialize GLFW");
        }

#if defined(__EMSCRIPTEN__)
        // On wasm, create the GLFW window using the actual canvas CSS size so
        // rendering starts with the correct surface extent from frame 0.
        double canvasCssWidth  = 0.0;
        double canvasCssHeight = 0.0;
        if (emscripten_get_element_css_size("#canvas", &canvasCssWidth, &canvasCssHeight) == EMSCRIPTEN_RESULT_SUCCESS)
        {
            const int cssWidth  = std::max(1, static_cast<int>(std::lround(canvasCssWidth)));
            const int cssHeight = std::max(1, static_cast<int>(std::lround(canvasCssHeight)));
            m_Extent            = {cssWidth, cssHeight};
        }
#endif

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, m_Resizable ? GLFW_TRUE : GLFW_FALSE);
        glfwWindowHint(GLFW_DECORATED, m_Decorated ? GLFW_TRUE : GLFW_FALSE);
        glfwWindowHint(GLFW_VISIBLE, m_Visible ? GLFW_TRUE : GLFW_FALSE);
        m_WindowHandle =
            glfwCreateWindow(std::max(1, m_Extent.x), std::max(1, m_Extent.y), m_Title.c_str(), nullptr, nullptr);
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
        glfwSetDropCallback(m_WindowHandle, &GLFWWindow::onDrop);
#if defined(__EMSCRIPTEN__)
        setDocumentAppTitle(m_Title.c_str());
#endif
        applyCursorVisibility();
        applyCursor();

#if defined(VULTRA_ENABLE_VULKAN) && VULTRA_ENABLE_VULKAN
        uint32_t    count      = 0;
        const auto* extensions = glfwGetRequiredInstanceExtensions(&count);
        if (extensions != nullptr && count > 0u)
        {
            m_VulkanExtensions.assign(extensions, extensions + count);
        }
#endif
    }

    GLFWWindow::~GLFWWindow()
    {
        if (m_OverrideCursorHandle)
        {
            glfwDestroyCursor(m_OverrideCursorHandle);
            m_OverrideCursorHandle = nullptr;
        }
        if (m_CustomCursorHandle)
        {
            glfwDestroyCursor(m_CustomCursorHandle);
            m_CustomCursorHandle = nullptr;
        }
        for (auto*& cursor : m_CursorHandles)
        {
            if (cursor)
            {
                glfwDestroyCursor(cursor);
                cursor = nullptr;
            }
        }
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
#if defined(__EMSCRIPTEN__)
        setDocumentAppTitle(m_Title.c_str());
#endif
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
        if (m_Cursor == cursor)
        {
            return *this;
        }
        m_Cursor = cursor;
        applyCursor();
        return *this;
    }

    os::Window& GLFWWindow::setCustomCursor(const CursorImage& cursorImage)
    {
        if (!cursorImage.valid())
        {
            return clearCustomCursor();
        }

        if (m_HasCustomCursor && m_CustomCursorImage && cursorImagesEqual(*m_CustomCursorImage, cursorImage))
        {
            return *this;
        }

#if defined(__EMSCRIPTEN__)
        m_CustomCursorImage = cursorImage;
        m_HasCustomCursor   = true;
#else
        auto* customCursor = createColorCursor(cursorImage);
        if (customCursor == nullptr)
        {
            return clearCustomCursor();
        }

        if (m_CustomCursorHandle)
        {
            glfwDestroyCursor(m_CustomCursorHandle);
        }
        m_CustomCursorHandle = customCursor;
        m_CustomCursorImage  = cursorImage;
        m_HasCustomCursor    = true;
#endif
        applyCursor();
        return *this;
    }

    os::Window& GLFWWindow::clearCustomCursor()
    {
        if (!m_HasCustomCursor && !m_CustomCursorImage.has_value())
        {
            return *this;
        }
        m_HasCustomCursor = false;
        m_CustomCursorImage.reset();
#if !defined(__EMSCRIPTEN__)
        if (m_CustomCursorHandle)
        {
            glfwDestroyCursor(m_CustomCursorHandle);
            m_CustomCursorHandle = nullptr;
        }
#endif
        applyCursor();
        return *this;
    }

    os::Window& GLFWWindow::setCursorOverride(const CursorImage& cursorImage)
    {
        if (!cursorImage.valid())
        {
            return clearCursorOverride();
        }

        if (m_HasCursorOverride && m_OverrideCursorImage && cursorImagesEqual(*m_OverrideCursorImage, cursorImage))
        {
            return *this;
        }

#if defined(__EMSCRIPTEN__)
        m_OverrideCursorImage = cursorImage;
        m_HasCursorOverride   = true;
#else
        auto* overrideCursor = createColorCursor(cursorImage);
        if (overrideCursor == nullptr)
        {
            return clearCursorOverride();
        }

        if (m_OverrideCursorHandle)
        {
            glfwDestroyCursor(m_OverrideCursorHandle);
        }
        m_OverrideCursorHandle = overrideCursor;
        m_OverrideCursorImage  = cursorImage;
        m_HasCursorOverride    = true;
#endif
        applyCursor();
        return *this;
    }

    os::Window& GLFWWindow::clearCursorOverride()
    {
        if (!m_HasCursorOverride && !m_OverrideCursorImage.has_value())
        {
            return *this;
        }

        m_HasCursorOverride = false;
        m_OverrideCursorImage.reset();
#if !defined(__EMSCRIPTEN__)
        if (m_OverrideCursorHandle)
        {
            glfwDestroyCursor(m_OverrideCursorHandle);
            m_OverrideCursorHandle = nullptr;
        }
#endif
        applyCursor();
        return *this;
    }

    os::Window& GLFWWindow::setCursorVisibility(bool cursorVisibility)
    {
        if (m_CursorVisibility == cursorVisibility)
        {
            return *this;
        }
        m_CursorVisibility = cursorVisibility;
        applyCursorVisibility();
        applyCursor();
        return *this;
    }

    os::Window& GLFWWindow::setMouseRelativeMode(bool mouseRelativeMode)
    {
        if (m_MouseRelativeMode == mouseRelativeMode)
        {
            return *this;
        }
        m_MouseRelativeMode = mouseRelativeMode;
        applyCursorVisibility();
        applyCursor();
        return *this;
    }

    os::Window& GLFWWindow::setResizable(bool resizable)
    {
        if (m_Resizable == resizable)
        {
            return *this;
        }
        m_Resizable = resizable;
        return *this;
    }

    os::Window& GLFWWindow::setFullscreen(bool fullscreen)
    {
        if (m_Fullscreen == fullscreen)
        {
            return *this;
        }
        m_Fullscreen = fullscreen;
        return *this;
    }

    os::Window& GLFWWindow::setDecorated(bool decorated)
    {
        if (m_Decorated == decorated)
        {
            return *this;
        }
        m_Decorated = decorated;
#if !defined(__EMSCRIPTEN__)
        if (m_WindowHandle)
        {
            glfwSetWindowAttrib(m_WindowHandle, GLFW_DECORATED, decorated ? GLFW_TRUE : GLFW_FALSE);
        }
#endif
        return *this;
    }

    os::Window& GLFWWindow::setVisible(bool visible)
    {
        if (m_Visible == visible)
        {
            return *this;
        }
        m_Visible = visible;
#if !defined(__EMSCRIPTEN__)
        if (m_WindowHandle)
        {
            if (m_Visible)
                glfwShowWindow(m_WindowHandle);
            else
                glfwHideWindow(m_WindowHandle);
        }
#endif
        return *this;
    }

    os::Window& GLFWWindow::centerOnScreen()
    {
#if defined(__EMSCRIPTEN__)
        return *this;
#else
        if (!m_WindowHandle)
            return *this;

        GLFWmonitor* targetMonitor = nullptr;
        int          monitorCount  = 0;
        GLFWmonitor** monitors     = glfwGetMonitors(&monitorCount);
        const int windowCenterX = m_Position.x + m_Extent.x / 2;
        const int windowCenterY = m_Position.y + m_Extent.y / 2;

        for (int i = 0; monitors != nullptr && i < monitorCount; ++i)
        {
            int mx = 0;
            int my = 0;
            int mw = 0;
            int mh = 0;
            glfwGetMonitorWorkarea(monitors[i], &mx, &my, &mw, &mh);
            if (windowCenterX >= mx && windowCenterX < mx + mw && windowCenterY >= my && windowCenterY < my + mh)
            {
                targetMonitor = monitors[i];
                break;
            }
        }

        if (!targetMonitor)
            targetMonitor = glfwGetPrimaryMonitor();
        if (!targetMonitor)
            return *this;

        int mx = 0;
        int my = 0;
        int mw = 0;
        int mh = 0;
        glfwGetMonitorWorkarea(targetMonitor, &mx, &my, &mw, &mh);
        return setPosition({mx + (mw - m_Extent.x) / 2, my + (mh - m_Extent.y) / 2});
#endif
    }

    rhi::Rect2D GLFWWindow::getContentArea() const
    {
        return rhi::Rect2D {.offset = {0, 0},
                            .extent = {static_cast<uint32_t>(std::max(m_FrameBufferExtent.x, 0)),
                                       static_cast<uint32_t>(std::max(m_FrameBufferExtent.y, 0))}};
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

    bool GLFWWindow::isMaximized() const
    {
#if defined(__EMSCRIPTEN__)
        return false;
#else
        return m_WindowHandle != nullptr && glfwGetWindowAttrib(m_WindowHandle, GLFW_MAXIMIZED) == GLFW_TRUE;
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
        if (!m_WindowHandle || !instance ||
            glfwCreateWindowSurface(instance, m_WindowHandle, nullptr, &surface) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create GLFW Vulkan surface");
        }
        return vk::SurfaceKHR {surface};
    }
#endif

    WGPUSurface GLFWWindow::createWebGPUSurface(WGPUInstance instance) const
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
            const auto prevExtent            = m_Extent;
            const auto prevFrameBufferExtent = m_FrameBufferExtent;

#if defined(__EMSCRIPTEN__)
            // Keep GLFW window logical size in sync with the real canvas CSS size.
            // Emscripten callback bridging may vary across environments, so we query
            // and update proactively every frame.
            double canvasCssWidth  = 0.0;
            double canvasCssHeight = 0.0;
            if (emscripten_get_element_css_size("#canvas", &canvasCssWidth, &canvasCssHeight) ==
                EMSCRIPTEN_RESULT_SUCCESS)
            {
                const int cssWidth  = std::max(1, static_cast<int>(std::lround(canvasCssWidth)));
                const int cssHeight = std::max(1, static_cast<int>(std::lround(canvasCssHeight)));
                m_Extent            = {cssWidth, cssHeight};

                // Sync canvas backing store size to CSS size * devicePixelRatio.
                // Without this, fullscreen may only change CSS size while framebuffer
                // remains at old pixel dimensions.
                const double dpr    = std::max(emscripten_get_device_pixel_ratio(), 1.0);
                const int    pixelW = std::max(1, static_cast<int>(std::lround(canvasCssWidth * dpr)));
                const int    pixelH = std::max(1, static_cast<int>(std::lround(canvasCssHeight * dpr)));
                emscripten_set_canvas_element_size("#canvas", pixelW, pixelH);

                int canvasPixelW = 0;
                int canvasPixelH = 0;
                if (emscripten_get_canvas_element_size("#canvas", &canvasPixelW, &canvasPixelH) ==
                    EMSCRIPTEN_RESULT_SUCCESS)
                {
                    m_FrameBufferExtent = {std::max(1, canvasPixelW), std::max(1, canvasPixelH)};
                }
                else
                {
                    glfwGetFramebufferSize(m_WindowHandle, &m_FrameBufferExtent.x, &m_FrameBufferExtent.y);
                }
            }
            else
#endif
            {
                glfwGetWindowSize(m_WindowHandle, &m_Extent.x, &m_Extent.y);
                glfwGetFramebufferSize(m_WindowHandle, &m_FrameBufferExtent.x, &m_FrameBufferExtent.y);
            }
            m_ShouldClose = glfwWindowShouldClose(m_WindowHandle) != 0;

            if (m_Extent != prevExtent || m_FrameBufferExtent != prevFrameBufferExtent)
            {
                os::GeneralWindowEvent generalEvent {};
                generalEvent.type = event::WindowEventType::eResized;
                emitEvent(generalEvent);
            }
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

    void GLFWWindow::minimize()
    {
        if (m_WindowHandle)
            glfwIconifyWindow(m_WindowHandle);
    }

    void GLFWWindow::maximize()
    {
        if (m_WindowHandle)
            glfwMaximizeWindow(m_WindowHandle);
    }

    void GLFWWindow::restore()
    {
        if (m_WindowHandle)
            glfwRestoreWindow(m_WindowHandle);
    }

    void GLFWWindow::applyCursorVisibility()
    {
        if (!m_WindowHandle)
            return;

#if defined(__EMSCRIPTEN__)
        applyCursor();
#else
        const int mode =
            m_MouseRelativeMode ? GLFW_CURSOR_DISABLED : (m_CursorVisibility ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_HIDDEN);
        if (m_LastAppliedCursorMode != mode)
        {
            glfwSetInputMode(m_WindowHandle, GLFW_CURSOR, mode);
            m_LastAppliedCursorMode   = mode;
            m_LastAppliedCursorHandle = nullptr;
            m_HasAppliedCursorHandle  = false;
        }
#endif
    }

    void GLFWWindow::applyCursor()
    {
        if (!m_WindowHandle)
            return;

#if defined(__EMSCRIPTEN__)
        const int visible = (m_CursorVisibility && !m_MouseRelativeMode) ? 1 : 0;
        if (m_HasCursorOverride && m_OverrideCursorImage && m_OverrideCursorImage->valid())
        {
            setCanvasCustomCursor(m_OverrideCursorImage->pixels.data(),
                                  m_OverrideCursorImage->width,
                                  m_OverrideCursorImage->height,
                                  m_OverrideCursorImage->hotX,
                                  m_OverrideCursorImage->hotY,
                                  visible);
        }
        else if (m_HasCustomCursor && m_CustomCursorImage && m_CustomCursorImage->valid())
        {
            setCanvasCustomCursor(m_CustomCursorImage->pixels.data(),
                                  m_CustomCursorImage->width,
                                  m_CustomCursorImage->height,
                                  m_CustomCursorImage->hotX,
                                  m_CustomCursorImage->hotY,
                                  visible);
        }
        else
        {
            setCanvasCursorStyle(static_cast<int>(m_Cursor), visible);
        }
#else
        if (m_MouseRelativeMode || !m_CursorVisibility)
        {
            m_LastAppliedCursorHandle = nullptr;
            m_HasAppliedCursorHandle  = false;
            return;
        }

        auto* cursorHandle =
            m_HasCursorOverride ?
                m_OverrideCursorHandle :
                (m_HasCustomCursor ? m_CustomCursorHandle :
                                     (m_Cursor == CursorType::eArrow ? nullptr : ensureCursor(m_Cursor)));
        if (m_HasAppliedCursorHandle && m_LastAppliedCursorHandle == cursorHandle)
        {
            return;
        }

        glfwSetCursor(m_WindowHandle, cursorHandle);
        m_LastAppliedCursorHandle = cursorHandle;
        m_HasAppliedCursorHandle  = true;
#endif
    }

    GLFWcursor* GLFWWindow::ensureCursor(CursorType cursor)
    {
        const auto index = static_cast<size_t>(cursor);
        if (index >= m_CursorHandles.size())
            return nullptr;
        if (m_CursorHandles[index])
            return m_CursorHandles[index];

        m_CursorHandles[index] = glfwCreateStandardCursor(toGLFWSystemCursor(cursor));
        return m_CursorHandles[index];
    }

    GLFWcursor* GLFWWindow::createColorCursor(const CursorImage& cursorImage)
    {
        if (!cursorImage.valid())
        {
            return nullptr;
        }

        GLFWimage glfwImage {};
        glfwImage.width  = cursorImage.width;
        glfwImage.height = cursorImage.height;
        glfwImage.pixels = const_cast<unsigned char*>(cursorImage.pixels.data());
        return glfwCreateCursor(&glfwImage, cursorImage.hotX, cursorImage.hotY);
    }

    void GLFWWindow::shutdown() { glfwTerminate(); }

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
        generalEvent.type        = event::WindowEventType::eMouseMotion;
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
        generalEvent.type       = event::WindowEventType::eMouseWheel;
        generalEvent.mouseWheel = event::MouseWheelEvent {
            .delta = {static_cast<float>(xoffset), static_cast<float>(yoffset)},
        };
        self->emitEvent(generalEvent);
    }

    void GLFWWindow::onDrop(GLFWwindow* windowHandle, int count, const char** paths)
    {
        auto* self = fromHandle(windowHandle);
        if (!self || count <= 0 || paths == nullptr)
        {
            return;
        }

        event::FileDropEvent drop {};
        drop.paths.reserve(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i)
        {
            if (paths[i] != nullptr && paths[i][0] != '\0')
                drop.paths.emplace_back(paths[i]);
        }
        if (drop.paths.empty())
            return;

        os::GeneralWindowEvent generalEvent {};
        generalEvent.type     = event::WindowEventType::eFileDrop;
        generalEvent.fileDrop = std::move(drop);
        self->emitEvent(generalEvent);
    }
} // namespace vultra::platform::glfw
