#pragma once

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
        // Initial extent is in base-DPI units; the platform applies the monitor content scale.
        Window(const char* title, Extent extent = {1280, 720});
        // Borrow a backend window that has no Window wrapper. It must outlive this wrapper.
        explicit Window(void* borrowed);
        static std::expected<std::unique_ptr<Window>, std::string> create(const char* title,
                                                                          Extent      extent = {1280, 720});
        static std::expected<std::unique_ptr<Window>, std::string> borrow(void* borrowed);
        ~Window();
        Window(const Window&)                                       = delete;
        Window&                            operator=(const Window&) = delete;
        bool                               poll();
        static void                        waitEvents(double seconds);
        void                               setTitle(const char* title);
        void                               setTitleSuffix(std::string suffix);
        std::string                        title() const;
        void                               setSize(Extent extent);
        void                               minimize();
        void                               restore();
        bool                               minimized() const;
        Extent                             framebufferSize() const;
        Extent                             size() const; // Logical window units, matching mouse coordinates.
        std::expected<Extent, std::string> framebufferSizeExpected() const;
        std::expected<Extent, std::string> sizeExpected() const;
        void                               setTextInputEnabled(bool enabled);
        void*                              nativeHandle() const;

        Input& input()
        {
            return m_Input;
        }

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

        struct Uninitialized
        {};

        explicit Window(Uninitialized)
        {
        }

        std::expected<void, std::string> initializeOwned(const char* title, Extent extent);
        std::expected<void, std::string> initializeBorrowed(void* borrowed);
        void                             updateTitle();
        static void                      dispatchEvent(const void* event);

        void*       m_Window         = nullptr;
        bool        m_Owned          = false;
        bool        m_CloseRequested = false;
        std::string m_Title;
        std::string m_TitleSuffix;
        Input       m_Input;
        // SDL dispatches queued events to the owning GUI context; GLFW uses callback chaining.
        std::function<void(const void*)> m_GuiEventHandler;
    };
} // namespace vultra
