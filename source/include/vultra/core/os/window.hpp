#pragma once
#include <vultra/core/input/input.hpp>
#include <vultra/core/math/extent.hpp>

#include <string>
struct GLFWwindow;

namespace vultra
{
    class Window
    {
    public:
        Window(const char* title, Extent extent = {1280, 720});
        explicit Window(GLFWwindow* borrowed); // Platform backend retains ownership.
        ~Window();
        Window(const Window&)            = delete;
        Window& operator=(const Window&) = delete;
        bool    poll();
        void    setTitle(const char* title);
        void    setTitleSuffix(std::string suffix);
        Extent  framebufferSize() const;
        Extent  size() const; // Logical window units, matching mouse coordinates.
        void*   nativeHandle() const;

        const Input& input() const
        {
            return m_Input;
        }

        GLFWwindow* handle() const
        {
            return m_Window;
        }

    private:
        GLFWwindow* m_Window = nullptr;
        bool        m_Owned  = true;
        std::string m_Title;
        std::string m_TitleSuffix;
        Input       m_Input;
    };
} // namespace vultra
