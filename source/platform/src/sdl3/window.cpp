#include <vultra/platform/sdl3/input.hpp>
#include <vultra/platform/window.hpp>

#include <vri/integration/vri_sdl3.h>

#include <stdexcept>

namespace vultra
{
    namespace
    {
        constexpr const char* kWindowProperty = "vultra.window";

        void checkSdl(bool result, const char* operation)
        {
            if (!result)
            {
                throw std::runtime_error(std::string(operation) + ": " + SDL_GetError());
            }
        }
    } // namespace

    Window::Window(const char* title, Extent extent) :
        m_Title(title)
    {
#if defined(__linux__)
        if (const auto* requested = platform::requestedWindowSystem())
        {
            checkSdl(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, requested, SDL_HINT_OVERRIDE),
                     "Select SDL video driver");
        }
#endif
        checkSdl(SDL_InitSubSystem(SDL_INIT_VIDEO), "Initialize SDL video");
        try
        {
            auto* window = SDL_CreateWindow(title,
                                            int(extent.width),
                                            int(extent.height),
                                            SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
            checkSdl(window != nullptr, "Create SDL window");
            m_Window = window;
            checkSdl(SDL_SetPointerProperty(SDL_GetWindowProperties(window), kWindowProperty, this),
                     "Attach window input");
            m_Input.setFocused((SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0);
        }
        catch (...)
        {
            SDL_DestroyWindow(static_cast<SDL_Window*>(m_Window));
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            throw;
        }
    }

    Window::Window(void* borrowed) :
        m_Window(borrowed),
        m_Owned(false)
    {
        if (!borrowed)
        {
            throw std::invalid_argument("Cannot wrap a null SDL window");
        }
        m_Title               = title();
        const auto properties = SDL_GetWindowProperties(static_cast<SDL_Window*>(m_Window));
        checkSdl(SDL_SetPointerProperty(properties, kWindowProperty, this), "Attach borrowed window input");
    }

    Window::~Window()
    {
        auto* window = static_cast<SDL_Window*>(m_Window);
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
        checkSdl(SDL_SetWindowTitle(static_cast<SDL_Window*>(m_Window), (m_Title + m_TitleSuffix).c_str()),
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
        checkSdl(SDL_GetWindowSizeInPixels(static_cast<SDL_Window*>(m_Window), &width, &height),
                 "Query framebuffer size");
        return {uint32_t(width), uint32_t(height)};
    }

    Extent Window::size() const
    {
        int width  = 0;
        int height = 0;
        checkSdl(SDL_GetWindowSize(static_cast<SDL_Window*>(m_Window), &width, &height), "Query window size");
        return {uint32_t(width), uint32_t(height)};
    }

    void Window::setTextInputEnabled(bool enabled)
    {
        auto* window = static_cast<SDL_Window*>(m_Window);
        if (SDL_TextInputActive(window) != enabled)
        {
            checkSdl(enabled ? SDL_StartTextInput(window) : SDL_StopTextInput(window),
                     enabled ? "Start SDL text input" : "Stop SDL text input");
        }
    }

    void Window::setSize(Extent extent)
    {
        checkSdl(SDL_SetWindowSize(static_cast<SDL_Window*>(m_Window), int(extent.width), int(extent.height)),
                 "Resize window");
    }

    void Window::minimize()
    {
        checkSdl(SDL_MinimizeWindow(static_cast<SDL_Window*>(m_Window)), "Minimize window");
    }

    void Window::restore()
    {
        checkSdl(SDL_RestoreWindow(static_cast<SDL_Window*>(m_Window)), "Restore window");
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
