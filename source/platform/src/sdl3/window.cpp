#include <vultra/platform/sdl3/input.hpp>
#include <vultra/platform/window.hpp>

#include <vri/integration/vri_sdl3.h>

#include <limits>
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
            return std::unexpected("Invalid SDL window title or extent");
#if defined(__linux__)
        auto requested = platform::requestedWindowSystemExpected();
        if (!requested)
            return std::unexpected(requested.error());
        if (*requested && !SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, *requested, SDL_HINT_OVERRIDE))
            return std::unexpected(std::string("Select SDL video driver: ") + SDL_GetError());
#endif
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
            return std::unexpected(std::string("Initialize SDL video: ") + SDL_GetError());
        auto* window = SDL_CreateWindow(title,
                                        int(extent.width),
                                        int(extent.height),
                                        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
        if (!window)
        {
            const std::string error = std::string("Create SDL window: ") + SDL_GetError();
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            return std::unexpected(error);
        }
        const auto scale = SDL_GetDisplayContentScale(SDL_GetDisplayForWindow(window));
        if (scale > 0 && scale != 1)
        {
            const double width   = extent.width * double(scale);
            const double height  = extent.height * double(scale);
            const int    maxSize = std::numeric_limits<int>::max();
            const bool   valid   = width >= 1 && height >= 1 && width <= maxSize && height <= maxSize;
            if (!valid || !SDL_SetWindowSize(window, int(width), int(height)))
            {
                const std::string error = valid ?
                                              std::string("Scale initial window to display DPI: ") + SDL_GetError() :
                                              "SDL window extent is out of range after DPI scaling";
                SDL_DestroyWindow(window);
                SDL_QuitSubSystem(SDL_INIT_VIDEO);
                return std::unexpected(error);
            }
        }
        if (!SDL_SetPointerProperty(SDL_GetWindowProperties(window), kWindowProperty, this))
        {
            const std::string error = std::string("Attach window input: ") + SDL_GetError();
            SDL_DestroyWindow(window);
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            return std::unexpected(error);
        }
        m_Window = window;
        m_Owned  = true;
        m_Title  = title;
        m_Input.setFocused((SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0);
        return {};
    }

    std::expected<void, std::string> Window::initializeBorrowed(void* borrowed)
    {
        if (!borrowed)
            return std::unexpected("Cannot wrap a null SDL window");
        auto*       window = static_cast<SDL_Window*>(borrowed);
        const char* title  = SDL_GetWindowTitle(window);
        if (!title)
            return std::unexpected(std::string("Read borrowed SDL window title: ") + SDL_GetError());
        if (!SDL_SetPointerProperty(SDL_GetWindowProperties(window), kWindowProperty, this))
            return std::unexpected(std::string("Attach borrowed window input: ") + SDL_GetError());
        m_Window = window;
        m_Title  = title;
        return {};
    }

    Window::~Window()
    {
        if (!m_Window)
            return;
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

    std::expected<Extent, std::string> Window::framebufferSizeExpected() const
    {
        int width  = 0;
        int height = 0;
        if (!SDL_GetWindowSizeInPixels(static_cast<SDL_Window*>(m_Window), &width, &height))
            return std::unexpected(std::string("Query framebuffer size: ") + SDL_GetError());
        if (width < 0 || height < 0)
            return std::unexpected("SDL returned a negative framebuffer extent");
        return Extent {uint32_t(width), uint32_t(height)};
    }

    std::expected<Extent, std::string> Window::sizeExpected() const
    {
        int width  = 0;
        int height = 0;
        if (!SDL_GetWindowSize(static_cast<SDL_Window*>(m_Window), &width, &height))
            return std::unexpected(std::string("Query window size: ") + SDL_GetError());
        if (width < 0 || height < 0)
            return std::unexpected("SDL returned a negative window extent");
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
