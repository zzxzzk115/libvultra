#include "vultra/core/os/window.hpp"
#include "vultra/core/base/base.hpp"
#include "vultra/core/base/common_context.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_vulkan.h>

namespace vultra
{
    namespace os
    {
        Window* Window::s_ActiveWindow = nullptr;

        Window::Window(Window&& other) noexcept :
            emitter {std::move(other)}, m_Title(std::move(other.m_Title)), m_Extent(other.m_Extent),
            m_Position(other.m_Position), m_CursorVisibility(other.m_CursorVisibility)
        {}

        Window::~Window()
        {
            SDL_DestroyWindow(m_SDL3WindowHandle);
            SDL_Quit();

            clear();
        }

        Window& Window::operator=(Window&& rhs) noexcept
        {
            if (this != &rhs)
            {
                emitter::operator=(std::move(rhs));
                m_Title            = std::move(rhs.m_Title);
                m_Extent           = rhs.m_Extent;
                m_Position         = rhs.m_Position;
                m_CursorVisibility = rhs.m_CursorVisibility;
            }

            return *this;
        }

        float Window::getPrimaryDisplayScale() { return SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay()); }

        Window& Window::getActiveWindow() { return *s_ActiveWindow; }

        Window& Window::setTitle(const std::string_view title)
        {
            m_Title = title;
            SDL_SetWindowTitle(m_SDL3WindowHandle, title.data());
            return *this;
        }

        Window& Window::setExtent(const Extent extent)
        {
            m_Extent = extent;
            SDL_SetWindowSize(m_SDL3WindowHandle, extent.x, extent.y);
            return *this;
        }

        Window& Window::setPosition(const Position position)
        {
            m_Position = position;
            SDL_SetWindowPosition(m_SDL3WindowHandle, position.x, position.y);
            return *this;
        }

        Window& Window::setCursor(const CursorType cursor)
        {
            m_Cursor = cursor;
            switch (cursor)
            {
                case CursorType::eArrow:
                    SDL_SetCursor(SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT));
                    break;
                case CursorType::eGrab:
                    SDL_SetCursor(SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_MOVE));
                    break;
                default:
                    SDL_SetCursor(SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT));
                    break;
            }
            return *this;
        }

        Window& Window::setCursorVisibility(const bool cursorVisibility)
        {
            if (m_CursorVisibility == cursorVisibility)
                return *this;

            m_CursorVisibility = cursorVisibility;
            if (cursorVisibility)
            {
                SDL_ShowCursor();
            }
            else
            {
                SDL_HideCursor();
            }
            return *this;
        }

        Window& Window::setMouseRelativeMode(const bool mouseRelativeMode)
        {
            m_MouseRelativeMode = mouseRelativeMode;
            SDL_SetWindowRelativeMouseMode(m_SDL3WindowHandle, mouseRelativeMode);
            return *this;
        }

        Window& Window::setResizable(const bool resizable)
        {
            m_Resizable = resizable;
            SDL_SetWindowResizable(m_SDL3WindowHandle, resizable);
            return *this;
        }

        Window& Window::setFullscreen(const bool fullscreen)
        {
            m_Fullscreen = fullscreen;
            SDL_SetWindowFullscreen(m_SDL3WindowHandle, fullscreen);
            return *this;
        }

        std::string_view Window::getTitle() const { return m_Title; }

        Window::Extent Window::getExtent() const { return m_Extent; }

        Window::Extent Window::getFrameBufferExtent() const { return m_FrameBufferExtent; }

        Window::Position Window::getPosition() const { return m_Position; }

        Window::CursorType Window::getCursor() const { return m_Cursor; }

        bool Window::getCursorVisibility() const { return m_CursorVisibility; }

        bool Window::getMouseRelativeMode() const { return m_MouseRelativeMode; }

        bool Window::isResizable() const { return m_Resizable; }

        bool Window::isFullscreen() const { return m_Fullscreen; }

        float Window::getDisplayScale() const
        {
            // int displayIndex = SDL_GetDisplayForWindow(m_SDL3WindowHandle);
            // return SDL_GetDisplayContentScale(displayIndex);
            return SDL_GetWindowDisplayScale(m_SDL3WindowHandle);
        }

        bool Window::shouldClose() const { return m_ShouldClose; }

        bool Window::isMinimized() const { return m_IsMinimized; }

        SDL_Window* Window::getSDL3WindowHandle() const { return m_SDL3WindowHandle; }

        // https://github.com/libsdl-org/SDL/blob/main/docs/README-migration.md#sdl_syswmh
        void* Window::getOSWindowHandle() const
        {
#if defined(SDL_PLATFORM_WIN32)
            return SDL_GetPointerProperty(
                SDL_GetWindowProperties(m_SDL3WindowHandle), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#elif defined(SDL_PLATFORM_MACOS)
            return SDL_GetPointerProperty(
                SDL_GetWindowProperties(m_SDL3WindowHandle), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
#elif defined(SDL_PLATFORM_LINUX)
            if (SDL_strcmp(SDL_GetCurrentVideoDriver(), "x11") == 0)
            {
                return reinterpret_cast<void*>(static_cast<uintptr_t>(SDL_GetNumberProperty(
                    SDL_GetWindowProperties(m_SDL3WindowHandle), SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0)));
            }
            else if (SDL_strcmp(SDL_GetCurrentVideoDriver(), "wayland") == 0)
            {
                return SDL_GetPointerProperty(
                    SDL_GetWindowProperties(m_SDL3WindowHandle), SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
            }
            else
            {
                throw std::logic_error("Unknown window system");
            }
#elif defined(SDL_PLATFORM_IOS)
            SDL_PropertiesID props = SDL_GetWindowProperties(m_SDL3WindowHandle);
            return SDL_GetPointerProperty(props, SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, nullptr);
#endif
        }

        Window::DriverType Window::getDriverType()
        {
#if defined(SDL_PLATFORM_WIN32)
            return DriverType::eWin32;
#elif defined(SDL_PLATFORM_MACOS)
            return DriverType::eCocoa;
#elif defined(SDL_PLATFORM_LINUX)
            if (SDL_strcmp(SDL_GetCurrentVideoDriver(), "x11") == 0)
            {
                return DriverType::eX11;
            }
            else if (SDL_strcmp(SDL_GetCurrentVideoDriver(), "wayland") == 0)
            {
                return DriverType::eWayland;
            }
            else
            {
                throw std::logic_error("Unknown window system");
            }
#elif defined(SDL_PLATFORM_IOS)
            return DriverType::eUIKit;
#endif
        }

        vk::SurfaceKHR Window::createVulkanSurface(vk::Instance instance) const
        {
            VULTRA_CUSTOM_ASSERT(m_SDL3WindowHandle);
            VkSurfaceKHR surface {nullptr};
            if (!SDL_Vulkan_CreateSurface(m_SDL3WindowHandle, instance, nullptr, &surface))
            {
                VULTRA_CORE_ERROR("[Window] Failed to create Vulkan surface!, Error: {}", SDL_GetError());
                throw std::runtime_error("Failed to create Vulkan surface");
            }

            return surface;
        }

        void Window::pollEvents()
        {
            m_ShouldClose = false;
            m_IsMinimized = false;

            SDL_Event event;
            while (SDL_PollEvent(&event))
            {
                if (event.type == SDL_EVENT_QUIT)
                    m_ShouldClose = true;
                if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                    event.window.windowID == SDL_GetWindowID(m_SDL3WindowHandle))
                    m_IsMinimized = true;

                // Update the window extent if it has changed
                if (event.type == SDL_EVENT_WINDOW_RESIZED)
                {
                    m_Extent.x = event.window.data1;
                    m_Extent.y = event.window.data2;

                    // Also update the framebuffer extent
                    SDL_GetWindowSizeInPixels(m_SDL3WindowHandle, &m_FrameBufferExtent.x, &m_FrameBufferExtent.y);
                }

                // Update the window position if it has changed
                if (event.type == SDL_EVENT_WINDOW_MOVED)
                {
                    m_Position.x = event.window.data1;
                    m_Position.y = event.window.data2;
                }

                publish<GeneralWindowEvent>({event.type, event});
            }

            if (SDL_GetWindowFlags(m_SDL3WindowHandle) & SDL_WINDOW_MINIMIZED)
            {
                SDL_Delay(10);
                m_IsMinimized = true;
            }
        }

        void Window::close() { m_ShouldClose = true; }

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

        Window Window::Builder::build() const
        {
            return Window {m_Title, m_Extent, m_Position, m_CursorVisibility, m_Resizable, m_Fullscreen};
        }

        Window::Window(const std::string_view title,
                       const Extent           extent,
                       const Position         position,
                       const bool             cursorVisible,
                       const bool             resizable,
                       const bool             fullscreen) :
            m_Title(title), m_Extent(extent), m_Position(position), m_CursorVisibility(cursorVisible),
            m_Resizable(resizable), m_Fullscreen(fullscreen)
        {
            // Setup SDL
            if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
            {
                VULTRA_CORE_ERROR("[Window] Failed to initialize SDL3, Error:{}", SDL_GetError());
                throw std::runtime_error("Failed to initialize SDL3");
            }

            // NOTE: vcpkg SDL3 must enable "vulkan" feature.
            // https://github.com/libsdl-org/SDL/issues/12162#issuecomment-2764608476
            SDL_WindowFlags windowFlags = SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_VULKAN;

            if (m_Resizable)
            {
                windowFlags |= SDL_WINDOW_RESIZABLE;
            }
            if (m_Fullscreen)
            {
                windowFlags |= SDL_WINDOW_FULLSCREEN;
            }

            if (!SDL_Vulkan_LoadLibrary(nullptr))
            {
                VULTRA_CORE_ERROR("[Window] Could not load Vulkan library! Error: {}", SDL_GetError());
                throw std::runtime_error("Could not load Vulkan library");
            }

            // Create SDL window
            float main_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
            m_SDL3WindowHandle =
                SDL_CreateWindow(m_Title.c_str(), m_Extent.x * main_scale, m_Extent.y * main_scale, windowFlags);
            if (m_SDL3WindowHandle == nullptr)
            {
                VULTRA_CORE_ERROR("[Window] Failed to create SDL3 window, Error:{}", SDL_GetError());
                throw std::runtime_error("Failed to create SDL3 window");
            }

            // Set position, extent, cursor visibility
            if (position != Position {0, 0})
            {
                setPosition(m_Position);
            }
            setCursorVisibility(m_CursorVisibility);

            // Setup framebuffer extent,
            // Why we need this?
            // Wayland & High DPI support.
            // https://github.com/ocornut/imgui/issues/8761
            SDL_GetWindowSizeInPixels(m_SDL3WindowHandle, &m_FrameBufferExtent.x, &m_FrameBufferExtent.y);

            // Set active window
            // NOTE: Now, only support single window application
            s_ActiveWindow = this;
        }
    } // namespace os
} // namespace vultra