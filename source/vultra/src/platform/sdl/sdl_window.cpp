#if !defined(__ANDROID__)

#include "vultra/platform/sdl/sdl_window.hpp"

#include "vultra/core/base/common_context.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_mouse.h>
#if defined(VULTRA_ENABLE_VULKAN) && VULTRA_ENABLE_VULKAN
#include <SDL3/SDL_vulkan.h>
#endif
#if defined(SDL_PLATFORM_MACOS)
#include <SDL3/SDL_metal.h>
#endif

#include <cstring>

namespace vultra::platform::sdl
{
    namespace
    {
        constexpr auto kSDLInitFlags = SDL_INIT_VIDEO | SDL_INIT_GAMEPAD;

        bool cursorImagesEqual(const os::Window::CursorImage& lhs, const os::Window::CursorImage& rhs)
        {
            return lhs.width == rhs.width && lhs.height == rhs.height && lhs.hotX == rhs.hotX && lhs.hotY == rhs.hotY &&
                   lhs.pixels == rhs.pixels;
        }

        SDL_SystemCursor toSDLSystemCursor(const os::Window::CursorType cursor)
        {
            switch (cursor)
            {
                case os::Window::CursorType::eGrab:
                    return SDL_SYSTEM_CURSOR_POINTER;
                case os::Window::CursorType::eLook:
                    return SDL_SYSTEM_CURSOR_CROSSHAIR;
                case os::Window::CursorType::eZoomIn:
                case os::Window::CursorType::eZoomOut:
                    return SDL_SYSTEM_CURSOR_DEFAULT;
                case os::Window::CursorType::eOrbit:
                    return SDL_SYSTEM_CURSOR_DEFAULT;
                case os::Window::CursorType::eTextInput:
                    return SDL_SYSTEM_CURSOR_TEXT;
                case os::Window::CursorType::eResizeNS:
                    return SDL_SYSTEM_CURSOR_NS_RESIZE;
                case os::Window::CursorType::eResizeEW:
                    return SDL_SYSTEM_CURSOR_EW_RESIZE;
                case os::Window::CursorType::eResizeNESW:
                    return SDL_SYSTEM_CURSOR_NESW_RESIZE;
                case os::Window::CursorType::eResizeNWSE:
                    return SDL_SYSTEM_CURSOR_NWSE_RESIZE;
                case os::Window::CursorType::eHand:
                    return SDL_SYSTEM_CURSOR_POINTER;
                case os::Window::CursorType::eNotAllowed:
                    return SDL_SYSTEM_CURSOR_NOT_ALLOWED;
                case os::Window::CursorType::eArrow:
                case os::Window::CursorType::eCount:
                default:
                    return SDL_SYSTEM_CURSOR_DEFAULT;
            }
        }
    }

    SDLWindow::SDLWindow(std::string_view title,
                         Extent           extent,
                         Position         position,
                         bool             cursorVisible,
                         bool             resizable,
                         bool             fullscreen) :
        m_Title(title), m_Extent(extent), m_Position(position), m_CursorVisibility(cursorVisible),
        m_Resizable(resizable), m_Fullscreen(fullscreen)
    {
        if (!SDL_Init(kSDLInitFlags))
        {
            VULTRA_CORE_ERROR("[SDLWindow] Failed to initialize SDL3, Error:{}", SDL_GetError());
            throw std::runtime_error("Failed to initialize SDL3");
        }

        SDL_WindowFlags windowFlags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
#if defined(VULTRA_ENABLE_VULKAN) && VULTRA_ENABLE_VULKAN
        uint32_t    extensionCount = 0;
        const auto* extensions     = SDL_Vulkan_GetInstanceExtensions(&extensionCount);
        m_VulkanExtensions.assign(extensions, extensions + extensionCount);
        windowFlags |= SDL_WINDOW_VULKAN;
#endif
        if (m_Resizable)
        {
            windowFlags |= SDL_WINDOW_RESIZABLE;
        }
        if (m_Fullscreen)
        {
            windowFlags |= SDL_WINDOW_FULLSCREEN;
        }

#if defined(VULTRA_ENABLE_VULKAN) && VULTRA_ENABLE_VULKAN
        if (!SDL_Vulkan_LoadLibrary(nullptr))
        {
            VULTRA_CORE_ERROR("[SDLWindow] Could not load Vulkan library! Error: {}", SDL_GetError());
            throw std::runtime_error("Could not load Vulkan library");
        }
#endif

#if __APPLE__
        float mainScale = 1.0f;
#else
        float mainScale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
#endif

        m_WindowHandle = SDL_CreateWindow(m_Title.c_str(), m_Extent.x * mainScale, m_Extent.y * mainScale, windowFlags);
        if (m_WindowHandle == nullptr)
        {
            VULTRA_CORE_ERROR("[SDLWindow] Failed to create SDL3 window, Error:{}", SDL_GetError());
            throw std::runtime_error("Failed to create SDL3 window");
        }

#if __APPLE__
        mainScale = SDL_GetWindowDisplayScale(m_WindowHandle);
#endif

        if (position != Position {0, 0})
        {
            setPosition(m_Position);
        }
        applyCursorVisibility();
        applyCursor();
        SDL_GetWindowSize(m_WindowHandle, &m_Extent.x, &m_Extent.y);
        SDL_GetWindowPosition(m_WindowHandle, &m_Position.x, &m_Position.y);
        SDL_GetWindowSizeInPixels(m_WindowHandle, &m_FrameBufferExtent.x, &m_FrameBufferExtent.y);

        VULTRA_CORE_INFO(
            "[SDLWindow] Created window '{}' ({}x{}), DPI: {}", m_Title, m_Extent.x, m_Extent.y, mainScale);
    }

    SDLWindow::~SDLWindow()
    {
        if (m_OverrideCursorHandle)
        {
            SDL_DestroyCursor(m_OverrideCursorHandle);
            m_OverrideCursorHandle = nullptr;
        }
        if (m_CustomCursorHandle)
        {
            SDL_DestroyCursor(m_CustomCursorHandle);
            m_CustomCursorHandle = nullptr;
        }
        for (auto*& cursor : m_CursorHandles)
        {
            if (cursor)
            {
                SDL_DestroyCursor(cursor);
                cursor = nullptr;
            }
        }
        if (m_WebGpuMetalView)
        {
#if defined(SDL_PLATFORM_MACOS)
            SDL_Metal_DestroyView(static_cast<SDL_MetalView>(m_WebGpuMetalView));
#endif
            m_WebGpuMetalView = nullptr;
        }

        if (m_WindowHandle)
        {
            SDL_DestroyWindow(m_WindowHandle);
            m_WindowHandle = nullptr;
        }
    }

    os::Window::DriverType SDLWindow::driverType() const { return translateDriverType(); }

    os::Window& SDLWindow::setTitle(std::string_view title)
    {
        m_Title = title;
        SDL_SetWindowTitle(m_WindowHandle, title.data());
        return *this;
    }

    os::Window& SDLWindow::setExtent(Extent extent)
    {
        m_Extent = extent;
        SDL_SetWindowSize(m_WindowHandle, extent.x, extent.y);
        return *this;
    }

    os::Window& SDLWindow::setPosition(Position position)
    {
        m_Position = position;
        SDL_SetWindowPosition(m_WindowHandle, position.x, position.y);
        return *this;
    }

    os::Window& SDLWindow::setCursor(CursorType cursor)
    {
        if (m_Cursor == cursor)
        {
            return *this;
        }
        m_Cursor = cursor;
        applyCursor();
        return *this;
    }

    os::Window& SDLWindow::setCustomCursor(const CursorImage& cursorImage)
    {
        if (!cursorImage.valid())
        {
            return clearCustomCursor();
        }

        if (m_HasCustomCursor && m_CustomCursorImage && cursorImagesEqual(*m_CustomCursorImage, cursorImage))
        {
            return *this;
        }

        auto* customCursor = createColorCursor(cursorImage);
        if (customCursor == nullptr)
        {
            return clearCustomCursor();
        }

        if (m_CustomCursorHandle)
        {
            SDL_DestroyCursor(m_CustomCursorHandle);
        }
        m_CustomCursorHandle = customCursor;
        m_CustomCursorImage  = cursorImage;
        m_HasCustomCursor    = true;
        applyCursor();
        return *this;
    }

    os::Window& SDLWindow::clearCustomCursor()
    {
        if (!m_HasCustomCursor && !m_CustomCursorImage.has_value())
        {
            return *this;
        }
        m_HasCustomCursor = false;
        m_CustomCursorImage.reset();
        if (m_CustomCursorHandle)
        {
            SDL_DestroyCursor(m_CustomCursorHandle);
            m_CustomCursorHandle = nullptr;
        }
        applyCursor();
        return *this;
    }

    os::Window& SDLWindow::setCursorOverride(const CursorImage& cursorImage)
    {
        if (!cursorImage.valid())
        {
            return clearCursorOverride();
        }

        if (m_HasCursorOverride && m_OverrideCursorImage && cursorImagesEqual(*m_OverrideCursorImage, cursorImage))
        {
            return *this;
        }

        auto* overrideCursor = createColorCursor(cursorImage);
        if (overrideCursor == nullptr)
        {
            return clearCursorOverride();
        }

        if (m_OverrideCursorHandle)
        {
            SDL_DestroyCursor(m_OverrideCursorHandle);
        }
        m_OverrideCursorHandle = overrideCursor;
        m_OverrideCursorImage  = cursorImage;
        m_HasCursorOverride    = true;
        applyCursor();
        return *this;
    }

    os::Window& SDLWindow::clearCursorOverride()
    {
        if (!m_HasCursorOverride && !m_OverrideCursorImage.has_value())
        {
            return *this;
        }

        m_HasCursorOverride = false;
        m_OverrideCursorImage.reset();
        if (m_OverrideCursorHandle)
        {
            SDL_DestroyCursor(m_OverrideCursorHandle);
            m_OverrideCursorHandle = nullptr;
        }
        applyCursor();
        return *this;
    }

    os::Window& SDLWindow::setCursorVisibility(bool cursorVisibility)
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

    os::Window& SDLWindow::setMouseRelativeMode(bool mouseRelativeMode)
    {
        if (m_MouseRelativeMode == mouseRelativeMode)
        {
            return *this;
        }
        m_MouseRelativeMode = mouseRelativeMode;
        SDL_SetWindowRelativeMouseMode(m_WindowHandle, mouseRelativeMode);
        applyCursorVisibility();
        applyCursor();
        return *this;
    }

    os::Window& SDLWindow::setResizable(bool resizable)
    {
        if (m_Resizable == resizable)
        {
            return *this;
        }
        m_Resizable = resizable;
        SDL_SetWindowResizable(m_WindowHandle, resizable);
        return *this;
    }

    os::Window& SDLWindow::setFullscreen(bool fullscreen)
    {
        if (m_Fullscreen == fullscreen)
        {
            return *this;
        }
        m_Fullscreen = fullscreen;
        SDL_SetWindowFullscreen(m_WindowHandle, fullscreen);
        return *this;
    }

    float SDLWindow::getDisplayScale() const { return SDL_GetWindowDisplayScale(m_WindowHandle); }

    void SDLWindow::applyCursorVisibility()
    {
        if (m_CursorVisibility && !m_MouseRelativeMode)
            SDL_ShowCursor();
        else
            SDL_HideCursor();
    }

    void SDLWindow::applyCursor()
    {
        if (!m_WindowHandle || !m_CursorVisibility || m_MouseRelativeMode)
            return;
        SDL_SetCursor(m_HasCursorOverride ? m_OverrideCursorHandle :
                      (m_HasCustomCursor ? m_CustomCursorHandle : ensureCursor(m_Cursor)));
    }

    SDL_Cursor* SDLWindow::ensureCursor(CursorType cursor)
    {
        const auto index = static_cast<size_t>(cursor);
        if (index >= m_CursorHandles.size())
            return nullptr;
        if (m_CursorHandles[index])
            return m_CursorHandles[index];

        m_CursorHandles[index] = SDL_CreateSystemCursor(toSDLSystemCursor(cursor));
        if (!m_CursorHandles[index])
        {
            m_CursorHandles[index] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
        }
        return m_CursorHandles[index];
    }

    SDL_Cursor* SDLWindow::createColorCursor(const CursorImage& cursorImage) const
    {
        if (!cursorImage.valid())
        {
            return nullptr;
        }

        SDL_Surface* surface = SDL_CreateSurface(cursorImage.width, cursorImage.height, SDL_PIXELFORMAT_RGBA32);
        if (!surface)
        {
            return nullptr;
        }

        std::memcpy(surface->pixels, cursorImage.pixels.data(), cursorImage.pixels.size());
        auto* colorCursor = SDL_CreateColorCursor(surface, cursorImage.hotX, cursorImage.hotY);
        SDL_DestroySurface(surface);
        return colorCursor;
    }

#if defined(VULTRA_ENABLE_VULKAN) && VULTRA_ENABLE_VULKAN
    std::span<const char* const> SDLWindow::getRequiredVulkanInstanceExtensions() const
    {
        return {m_VulkanExtensions.data(), m_VulkanExtensions.size()};
    }

    vk::SurfaceKHR SDLWindow::createVulkanSurface(vk::Instance instance) const
    {
        VkSurfaceKHR surface {VK_NULL_HANDLE};
        if (!SDL_Vulkan_CreateSurface(m_WindowHandle, instance, nullptr, &surface))
        {
            VULTRA_CORE_ERROR("[SDLWindow] Failed to create Vulkan surface!, Error: {}", SDL_GetError());
            throw std::runtime_error("Failed to create Vulkan surface");
        }

        return vk::SurfaceKHR {surface};
    }
#endif

    WGPUSurface SDLWindow::createWebGPUSurface(const WGPUInstance instance) const
    {
#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU
        if (instance == nullptr)
        {
            VULTRA_CORE_ERROR("[SDLWindow] Cannot create WebGPU surface: instance is null");
            throw std::runtime_error("WebGPU instance is null");
        }

#if defined(SDL_PLATFORM_MACOS)
        if (!m_WebGpuMetalView)
        {
            m_WebGpuMetalView = SDL_Metal_CreateView(m_WindowHandle);
        }
        if (!m_WebGpuMetalView)
        {
            VULTRA_CORE_ERROR("[SDLWindow] Failed to create SDL metal view for WebGPU surface");
            throw std::runtime_error("Failed to create SDL metal view");
        }

        void* metalLayer = SDL_Metal_GetLayer(static_cast<SDL_MetalView>(m_WebGpuMetalView));
        if (!metalLayer)
        {
            VULTRA_CORE_ERROR("[SDLWindow] Failed to get CAMetalLayer from SDL metal view");
            throw std::runtime_error("Failed to get CAMetalLayer");
        }

        WGPUSurfaceSourceMetalLayer source {};
        source.chain.sType = WGPUSType_SurfaceSourceMetalLayer;
        source.layer       = metalLayer;

        WGPUSurfaceDescriptor descriptor {};
        descriptor.nextInChain = reinterpret_cast<const WGPUChainedStruct*>(&source);

        auto surface = wgpuInstanceCreateSurface(instance, &descriptor);
        if (surface == nullptr)
        {
            VULTRA_CORE_ERROR("[SDLWindow] Failed to create WebGPU surface from CAMetalLayer");
            throw std::runtime_error("Failed to create WebGPU surface");
        }

        return surface;
#else
        VULTRA_CORE_ERROR("[SDLWindow] WebGPU surface creation is not implemented for this platform");
        throw std::runtime_error("WebGPU surface creation is not implemented for this platform");
#endif
#else
        (void)instance;
        VULTRA_CORE_ERROR("[SDLWindow] WebGPU is disabled for this build");
        throw std::runtime_error("WebGPU is disabled for this build");
#endif
    }

    void SDLWindow::pollEvents(int)
    {
        m_ShouldClose = false;
        m_IsMinimized = false;

        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            os::GeneralWindowEvent generalEvent {};
            generalEvent.nativeEvent       = &event;
            generalEvent.nativeEventSource = event::NativeEventSource::eSDL3;

            switch (event.type)
            {
                case SDL_EVENT_QUIT:
                    m_ShouldClose     = true;
                    generalEvent.type = event::WindowEventType::eQuit;
                    emitEvent(generalEvent);
                    break;

                case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                    if (event.window.windowID == SDL_GetWindowID(m_WindowHandle))
                    {
                        m_ShouldClose     = true;
                        generalEvent.type = event::WindowEventType::eCloseRequested;
                        emitEvent(generalEvent);
                    }
                    break;

                case SDL_EVENT_WINDOW_RESIZED:
                    SDL_GetWindowSize(m_WindowHandle, &m_Extent.x, &m_Extent.y);
                    SDL_GetWindowSizeInPixels(m_WindowHandle, &m_FrameBufferExtent.x, &m_FrameBufferExtent.y);
                    generalEvent.type = event::WindowEventType::eResized;
                    emitEvent(generalEvent);
                    break;

                case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                    SDL_GetWindowSize(m_WindowHandle, &m_Extent.x, &m_Extent.y);
                    SDL_GetWindowSizeInPixels(m_WindowHandle, &m_FrameBufferExtent.x, &m_FrameBufferExtent.y);
                    generalEvent.type = event::WindowEventType::eResized;
                    emitEvent(generalEvent);
                    break;

                case SDL_EVENT_WINDOW_MOVED:
                    SDL_GetWindowPosition(m_WindowHandle, &m_Position.x, &m_Position.y);
                    generalEvent.type = event::WindowEventType::eMoved;
                    emitEvent(generalEvent);
                    break;

                case SDL_EVENT_KEY_DOWN:
                    generalEvent.type = event::WindowEventType::eKeyDown;
                    generalEvent.key =
                        event::KeyEvent {.key = translateKeyCode(event.key.scancode), .repeat = event.key.repeat};
                    emitEvent(generalEvent);
                    break;

                case SDL_EVENT_KEY_UP:
                    generalEvent.type = event::WindowEventType::eKeyUp;
                    generalEvent.key  = event::KeyEvent {.key = translateKeyCode(event.key.scancode), .repeat = false};
                    emitEvent(generalEvent);
                    break;

                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    generalEvent.type        = event::WindowEventType::eMouseButtonDown;
                    generalEvent.mouseButton = event::MouseButtonEvent {
                        .button = translateMouseCode(event.button.button),
                        .clicks = event.button.clicks,
                    };
                    emitEvent(generalEvent);
                    break;

                case SDL_EVENT_MOUSE_BUTTON_UP:
                    generalEvent.type        = event::WindowEventType::eMouseButtonUp;
                    generalEvent.mouseButton = event::MouseButtonEvent {
                        .button = translateMouseCode(event.button.button),
                        .clicks = event.button.clicks,
                    };
                    emitEvent(generalEvent);
                    break;

                case SDL_EVENT_MOUSE_MOTION:
                    generalEvent.type        = event::WindowEventType::eMouseMotion;
                    generalEvent.mouseMotion = event::MouseMotionEvent {
                        .position = {event.motion.x, event.motion.y},
                        .delta    = {event.motion.xrel, event.motion.yrel},
                    };
                    emitEvent(generalEvent);
                    break;

                case SDL_EVENT_MOUSE_WHEEL:
                    generalEvent.type       = event::WindowEventType::eMouseWheel;
                    generalEvent.mouseWheel = event::MouseWheelEvent {.delta = {event.wheel.x, event.wheel.y}};
                    emitEvent(generalEvent);
                    break;
            }
        }

        if (SDL_GetWindowFlags(m_WindowHandle) & SDL_WINDOW_MINIMIZED)
        {
            SDL_Delay(10);
            m_IsMinimized = true;
        }
    }

    void SDLWindow::close() { m_ShouldClose = true; }

    void SDLWindow::shutdown() { SDL_Quit(); }

    os::Window::DriverType SDLWindow::translateDriverType()
    {
#if defined(SDL_PLATFORM_WIN32)
        return DriverType::eWin32;
#elif defined(SDL_PLATFORM_MACOS)
        return DriverType::eCocoa;
#elif defined(SDL_PLATFORM_IOS)
        return DriverType::eUIKit;
#elif defined(SDL_PLATFORM_LINUX)
        if (SDL_strcmp(SDL_GetCurrentVideoDriver(), "x11") == 0)
        {
            return DriverType::eX11;
        }
        if (SDL_strcmp(SDL_GetCurrentVideoDriver(), "wayland") == 0)
        {
            return DriverType::eWayland;
        }
#endif
        return DriverType::eUnknown;
    }

    KeyCode SDLWindow::translateKeyCode(const int scancode)
    {
        switch (scancode)
        {
            case SDL_SCANCODE_A:
                return KeyCode::eA;
            case SDL_SCANCODE_B:
                return KeyCode::eB;
            case SDL_SCANCODE_C:
                return KeyCode::eC;
            case SDL_SCANCODE_D:
                return KeyCode::eD;
            case SDL_SCANCODE_E:
                return KeyCode::eE;
            case SDL_SCANCODE_F:
                return KeyCode::eF;
            case SDL_SCANCODE_G:
                return KeyCode::eG;
            case SDL_SCANCODE_H:
                return KeyCode::eH;
            case SDL_SCANCODE_I:
                return KeyCode::eI;
            case SDL_SCANCODE_J:
                return KeyCode::eJ;
            case SDL_SCANCODE_K:
                return KeyCode::eK;
            case SDL_SCANCODE_L:
                return KeyCode::eL;
            case SDL_SCANCODE_M:
                return KeyCode::eM;
            case SDL_SCANCODE_N:
                return KeyCode::eN;
            case SDL_SCANCODE_O:
                return KeyCode::eO;
            case SDL_SCANCODE_P:
                return KeyCode::eP;
            case SDL_SCANCODE_Q:
                return KeyCode::eQ;
            case SDL_SCANCODE_R:
                return KeyCode::eR;
            case SDL_SCANCODE_S:
                return KeyCode::eS;
            case SDL_SCANCODE_T:
                return KeyCode::eT;
            case SDL_SCANCODE_U:
                return KeyCode::eU;
            case SDL_SCANCODE_V:
                return KeyCode::eV;
            case SDL_SCANCODE_W:
                return KeyCode::eW;
            case SDL_SCANCODE_X:
                return KeyCode::eX;
            case SDL_SCANCODE_Y:
                return KeyCode::eY;
            case SDL_SCANCODE_Z:
                return KeyCode::eZ;
            case SDL_SCANCODE_0:
                return KeyCode::eNum0;
            case SDL_SCANCODE_1:
                return KeyCode::eNum1;
            case SDL_SCANCODE_2:
                return KeyCode::eNum2;
            case SDL_SCANCODE_3:
                return KeyCode::eNum3;
            case SDL_SCANCODE_4:
                return KeyCode::eNum4;
            case SDL_SCANCODE_5:
                return KeyCode::eNum5;
            case SDL_SCANCODE_6:
                return KeyCode::eNum6;
            case SDL_SCANCODE_7:
                return KeyCode::eNum7;
            case SDL_SCANCODE_8:
                return KeyCode::eNum8;
            case SDL_SCANCODE_9:
                return KeyCode::eNum9;
            case SDL_SCANCODE_RETURN:
                return KeyCode::eReturn;
            case SDL_SCANCODE_ESCAPE:
                return KeyCode::eEscape;
            case SDL_SCANCODE_BACKSPACE:
                return KeyCode::eBackspace;
            case SDL_SCANCODE_TAB:
                return KeyCode::eTab;
            case SDL_SCANCODE_SPACE:
                return KeyCode::eSpace;
            case SDL_SCANCODE_MINUS:
                return KeyCode::eMinus;
            case SDL_SCANCODE_EQUALS:
                return KeyCode::eEquals;
            case SDL_SCANCODE_LEFTBRACKET:
                return KeyCode::eLeftBracket;
            case SDL_SCANCODE_RIGHTBRACKET:
                return KeyCode::eRightBracket;
            case SDL_SCANCODE_BACKSLASH:
                return KeyCode::eBackslash;
            case SDL_SCANCODE_SEMICOLON:
                return KeyCode::eSemicolon;
            case SDL_SCANCODE_APOSTROPHE:
                return KeyCode::eApostrophe;
            case SDL_SCANCODE_GRAVE:
                return KeyCode::eGrave;
            case SDL_SCANCODE_COMMA:
                return KeyCode::eComma;
            case SDL_SCANCODE_PERIOD:
                return KeyCode::ePeriod;
            case SDL_SCANCODE_SLASH:
                return KeyCode::eSlash;
            case SDL_SCANCODE_LSHIFT:
                return KeyCode::eLShift;
            case SDL_SCANCODE_RSHIFT:
                return KeyCode::eRShift;
            case SDL_SCANCODE_LCTRL:
                return KeyCode::eLCtrl;
            case SDL_SCANCODE_RCTRL:
                return KeyCode::eRCtrl;
            case SDL_SCANCODE_LALT:
                return KeyCode::eLAlt;
            case SDL_SCANCODE_RALT:
                return KeyCode::eRAlt;
            case SDL_SCANCODE_LEFT:
                return KeyCode::eLeft;
            case SDL_SCANCODE_RIGHT:
                return KeyCode::eRight;
            case SDL_SCANCODE_UP:
                return KeyCode::eUp;
            case SDL_SCANCODE_DOWN:
                return KeyCode::eDown;
            default:
                return KeyCode::eUnknown;
        }
    }

    MouseCode SDLWindow::translateMouseCode(const uint8_t button)
    {
        switch (button)
        {
            case SDL_BUTTON_LEFT:
                return MouseCode::eLeft;
            case SDL_BUTTON_MIDDLE:
                return MouseCode::eMiddle;
            case SDL_BUTTON_RIGHT:
                return MouseCode::eRight;
            case SDL_BUTTON_X1:
                return MouseCode::eX1;
            case SDL_BUTTON_X2:
                return MouseCode::eX2;
            default:
                return MouseCode::eLeft;
        }
    }
} // namespace vultra::platform::sdl

#endif
