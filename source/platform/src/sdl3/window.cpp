#include <vultra/platform/sdl3/input.hpp>
#include <vultra/platform/window.hpp>

#include <vri/integration/vri_sdl3.h>

#include <utility>

namespace vultra
{
    namespace
    {
        constexpr const char* kWindowProperty = "vultra.window";

        std::unexpected<Error> sdlError(const char* operation)
        {
            return makeError("{}: {}", operation, SDL_GetError());
        }

        // SDL rejects these calls on a live window only when the handle itself is invalid.
        void requireSdl(bool result, const char* operation)
        {
            if (!result)
            {
                panic(sdlError(operation).error().message);
            }
        }
    } // namespace

    Window::Window(void* window, bool owned, std::string title) :
        m_Window(window),
        m_Owned(owned),
        m_Title(std::move(title))
    {
    }

    std::expected<std::unique_ptr<Window>, Error> Window::create(const char* title, Extent extent)
    {
#if defined(__linux__)
        const auto requested = platform::requestedWindowSystem();
        if (!requested)
        {
            return std::unexpected(requested.error());
        }
        if (*requested && !SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, *requested, SDL_HINT_OVERRIDE))
        {
            return sdlError("Select SDL video driver");
        }
#endif
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
        {
            return sdlError("Initialize SDL video");
        }
        // SDL's native window coordinates include the content scale on Windows/X11; macOS and Wayland
        // report 1 here and expose density through the framebuffer size instead. Create the window
        // hidden at its final size so the user never sees an unscaled window being resized.
        const auto scaled = [extent](float scale)
        {
            scale = scale > 0 ? scale : 1;
            return std::pair {int(float(extent.width) * scale), int(float(extent.height) * scale)};
        };
        const auto expected = scaled(SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay()));
#if defined(__APPLE__)
        // Vulkan (MoltenVK) and Metal both present through the CAMetalLayer VRI attaches; SDL_WINDOW_VULKAN
        // would also require SDL to load a Vulkan loader, which a Metal build does not link.
        constexpr SDL_WindowFlags kSurfaceFlag = SDL_WINDOW_METAL;
#else
        constexpr SDL_WindowFlags kSurfaceFlag = SDL_WINDOW_VULKAN;
#endif
        auto* handle =
            SDL_CreateWindow(title,
                             expected.first,
                             expected.second,
                             kSurfaceFlag | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN);
        if (!handle)
        {
            auto error = sdlError("Create SDL window");
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            return error;
        }
        // From here the Window owns the handle and the video subsystem reference.
        std::unique_ptr<Window> window(new Window(handle, true, title));
        // The window manager may have placed it on a display with another content scale.
        const auto actual = scaled(SDL_GetDisplayContentScale(SDL_GetDisplayForWindow(handle)));
        if (actual != expected && !SDL_SetWindowSize(handle, actual.first, actual.second))
        {
            return sdlError("Scale initial window to display DPI");
        }
        if (!SDL_ShowWindow(handle))
        {
            return sdlError("Show SDL window");
        }
        if (!SDL_SetPointerProperty(SDL_GetWindowProperties(handle), kWindowProperty, window.get()))
        {
            return sdlError("Attach window input");
        }
        window->m_Input.setFocused((SDL_GetWindowFlags(handle) & SDL_WINDOW_INPUT_FOCUS) != 0);
        return window;
    }

    std::expected<std::unique_ptr<Window>, Error> Window::borrow(void* borrowed)
    {
        if (!borrowed)
        {
            return makeError("Cannot wrap a null SDL window");
        }
        auto*                   handle = static_cast<SDL_Window*>(borrowed);
        std::unique_ptr<Window> window(new Window(borrowed, false, SDL_GetWindowTitle(handle)));
        if (!SDL_SetPointerProperty(SDL_GetWindowProperties(handle), kWindowProperty, window.get()))
        {
            return sdlError("Attach borrowed window input");
        }
        return window;
    }

    Window::~Window()
    {
        auto* window = static_cast<SDL_Window*>(m_Window);
        // Clearing a property this window never set is harmless.
        SDL_ClearProperty(SDL_GetWindowProperties(window), kWindowProperty);
        if (m_Owned)
        {
            SDL_DestroyWindow(window);
            // SDL itself retains the video subsystem until the last owned window releases it.
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
        }
    }

    void Window::dispatchEvent(const void* nativeEvent)
    {
        const auto& event = *static_cast<const SDL_Event*>(nativeEvent);
        if (event.type == SDL_EVENT_QUIT)
        {
            int    count   = 0;
            auto** windows = SDL_GetWindows(&count);
            for (int i = 0; i < count; ++i)
            {
                auto* owner = static_cast<Window*>(
                    SDL_GetPointerProperty(SDL_GetWindowProperties(windows[i]), kWindowProperty, nullptr));
                if (owner)
                {
                    owner->m_CloseRequested = true;
                }
            }
            SDL_free(windows);
            return;
        }
        auto* window = SDL_GetWindowFromEvent(&event);
        if (!window)
        {
            // Clipboard and gamepad changes have no window ID; notify each main GUI context once.
            int    count   = 0;
            auto** windows = SDL_GetWindows(&count);
            for (int i = 0; i < count; ++i)
            {
                auto* owner = static_cast<Window*>(
                    SDL_GetPointerProperty(SDL_GetWindowProperties(windows[i]), kWindowProperty, nullptr));
                if (owner && owner->m_Owned && owner->m_GuiEventHandler)
                {
                    owner->m_GuiEventHandler(&event);
                }
            }
            SDL_free(windows);
            return;
        }
        auto* owner =
            static_cast<Window*>(SDL_GetPointerProperty(SDL_GetWindowProperties(window), kWindowProperty, nullptr));
        if (!owner)
        {
            return;
        }
        if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
        {
            owner->m_CloseRequested = true;
        }
        platform::processSdlInput(owner->m_Input, event);
        if (owner->m_GuiEventHandler)
        {
            owner->m_GuiEventHandler(&event);
        }
    }

    bool Window::poll()
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            dispatchEvent(&event);
        }
        m_Input.advanceFrame();
        return !m_CloseRequested;
    }

    void Window::requestClose()
    {
        m_CloseRequested = true;
    }

    void Window::waitEvents(double seconds)
    {
        SDL_Event event;
        if (SDL_WaitEventTimeout(&event, int(seconds * 1000)))
        {
            dispatchEvent(&event);
        }
    }

    void Window::updateTitle()
    {
        requireSdl(SDL_SetWindowTitle(static_cast<SDL_Window*>(m_Window), (m_Title + m_TitleSuffix).c_str()),
                   "Set window title");
    }

    std::string Window::title() const
    {
        return SDL_GetWindowTitle(static_cast<SDL_Window*>(m_Window));
    }

    Extent Window::framebufferSize() const
    {
        int width  = 0;
        int height = 0;
        requireSdl(SDL_GetWindowSizeInPixels(static_cast<SDL_Window*>(m_Window), &width, &height),
                   "Query framebuffer size");
        return {uint32_t(width), uint32_t(height)};
    }

    Extent Window::size() const
    {
        int width  = 0;
        int height = 0;
        requireSdl(SDL_GetWindowSize(static_cast<SDL_Window*>(m_Window), &width, &height), "Query window size");
        return {uint32_t(width), uint32_t(height)};
    }

    void Window::setTextInputEnabled(bool enabled)
    {
        auto* window = static_cast<SDL_Window*>(m_Window);
        if (SDL_TextInputActive(window) != enabled)
        {
            requireSdl(enabled ? SDL_StartTextInput(window) : SDL_StopTextInput(window),
                       enabled ? "Start SDL text input" : "Stop SDL text input");
        }
    }

    void Window::setSize(Extent extent)
    {
        requireSdl(SDL_SetWindowSize(static_cast<SDL_Window*>(m_Window), int(extent.width), int(extent.height)),
                   "Resize window");
    }

    void Window::minimize()
    {
        requireSdl(SDL_MinimizeWindow(static_cast<SDL_Window*>(m_Window)), "Minimize window");
    }

    void Window::restore()
    {
        requireSdl(SDL_RestoreWindow(static_cast<SDL_Window*>(m_Window)), "Restore window");
    }

    bool Window::minimized() const
    {
        return (SDL_GetWindowFlags(static_cast<SDL_Window*>(m_Window)) & SDL_WINDOW_MINIMIZED) != 0;
    }

    namespace platform
    {
        VriWindowHandle nativeWindow(const Window& window)
        {
            return vriWindowHandleFromSDL3(static_cast<SDL_Window*>(window.handle()));
        }
    } // namespace platform
} // namespace vultra
