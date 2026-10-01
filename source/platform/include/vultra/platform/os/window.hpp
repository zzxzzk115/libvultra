#pragma once

#include <vultra/core/math/extent.hpp>
#include <vultra/platform/input/input.hpp>

#include <functional>
#include <string>

namespace vultra
{
    class EditorGui;

    class Window
    {
    public:
        Window(const char* title, Extent extent = {1280, 720});
        // Borrow a backend window that has no Window wrapper. It must outlive this wrapper.
        explicit Window(void* borrowed);
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
        void*       nativeHandle() const;

        const Input& input() const
        {
            return m_Input;
        }

        // Used only at the platform boundary: GLFWwindow* or SDL_Window* for the selected backend.
        void* handle() const
        {
            return m_Window;
        }

    private:
        friend class EditorGui;
        void        updateTitle();
        static void dispatchEvent(const void* event);

        void*       m_Window         = nullptr;
        bool        m_Owned          = true;
        bool        m_CloseRequested = false;
        std::string m_Title;
        std::string m_TitleSuffix;
        Input       m_Input;
        // SDL dispatches queued events to the owning GUI context; GLFW uses callback chaining.
        std::function<void(const void*)> m_GuiEventHandler;
    };
} // namespace vultra
