#include <vultra/platform/windows/window.hpp>

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <windows.h>

namespace vultra
{
    void* Window::nativeHandle() const
    {
        return glfwGetWin32Window(handle());
    }

    namespace platform
    {
        VriWindowHandle nativeWindow(const Window& window)
        {
            VriWindowHandle result {};
            result.type                   = VriWindowSystem_Win32;
            result.handle.win32.hwnd      = window.nativeHandle();
            result.handle.win32.hinstance = GetModuleHandle(nullptr);
            return result;
        }
    } // namespace platform
} // namespace vultra
