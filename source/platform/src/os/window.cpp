#include <vultra/platform/os/window.hpp>
#include <vultra/platform/window.hpp>

#include <cstdlib>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace vultra
{
    void Window::setTitle(const char* title)
    {
        m_Title = title;
        updateTitle();
    }

    void Window::setTitleSuffix(std::string suffix)
    {
        m_TitleSuffix = std::move(suffix);
        updateTitle();
    }

    void* Window::nativeHandle() const
    {
        const auto native = platform::nativeWindow(*this);
        switch (native.type)
        {
            case VriWindowSystem_Win32:
                return native.handle.win32.hwnd;
            case VriWindowSystem_Xlib:
                return reinterpret_cast<void*>(native.handle.xlib.window);
            case VriWindowSystem_Wayland:
                return native.handle.wayland.surface;
            default:
                throw std::runtime_error("Unsupported native window system");
        }
    }

    namespace platform
    {
        const char* requestedWindowSystem()
        {
            const auto* value = std::getenv("VULTRA_WINDOW_SYSTEM");
            if (!value || std::string_view(value).empty())
            {
                return nullptr;
            }
            if (std::string_view(value) != "x11" && std::string_view(value) != "wayland")
            {
                throw std::invalid_argument("VULTRA_WINDOW_SYSTEM must be x11 or wayland");
            }
            return value;
        }
    } // namespace platform
} // namespace vultra
