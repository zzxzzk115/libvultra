#pragma once

#include <vultra/core/base/error.hpp>
#include <vultra/core/math/extent.hpp>
#include <vultra/platform/input/input.hpp>

#include <expected>
#include <functional>
#include <memory>
#include <string>

namespace vultra
{
    class EditorGui;

    class Window
    {
    public:
        // Initial extent is in base-DPI units; the platform applies the monitor content scale. The window
        // registers its own address with SDL, so it lives behind a unique_ptr and never moves.
        [[nodiscard]] static std::expected<std::unique_ptr<Window>, Error> create(const char* title,
                                                                                  Extent      extent = {1280, 720});
        // Borrow a backend window that has no Window wrapper. It must outlive this wrapper.
        [[nodiscard]] static std::expected<std::unique_ptr<Window>, Error> borrow(void* borrowed);
        ~Window();
        Window(const Window&)                = delete;
        Window&     operator=(const Window&) = delete;
        bool        poll();
        static void waitEvents(double seconds);
        void        setTitle(const char* title);
        void        setTitleSuffix(std::string suffix);
        std::string title() const;
        void        setSize(Extent extent);
        void        minimize();
        void        restore();
        bool        minimized() const;
        Extent      framebufferSize() const;
        Extent      size() const; // Logical window units, matching mouse coordinates.
        void        setTextInputEnabled(bool enabled);
        // The OS window handle; an error when the backend reports a window system Vultra does not know.
        [[nodiscard]] std::expected<void*, Error> nativeHandle() const;

        Input& input()
        {
            return m_Input;
        }

        const Input& input() const
        {
            return m_Input;
        }

        // Used only at the platform boundary: the SDL_Window*.
        void* handle() const
        {
            return m_Window;
        }

    private:
        Window(void* window, bool owned, std::string title);
        friend class EditorGui;
        void        updateTitle();
        static void dispatchEvent(const void* event);

        void*       m_Window         = nullptr;
        bool        m_Owned          = true;
        bool        m_CloseRequested = false;
        std::string m_Title;
        std::string m_TitleSuffix;
        Input       m_Input;
        // SDL dispatches queued events to the owning GUI context.
        std::function<void(const void*)> m_GuiEventHandler;
    };
} // namespace vultra
