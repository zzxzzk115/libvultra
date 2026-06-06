#include "vultra/function/imgui/imgui_dpi.hpp"

#include <algorithm>

namespace vultra
{
    namespace
    {
        // One symbol per factor in the vultra library; UI-thread only, so no synchronization needed.
        float g_ImGuiDpiScale  = 1.0f; // OS display DPI
        float g_ImGuiUserScale = 1.0f; // editor "Application Scale" setting
    } // namespace

    void  setImGuiDpiScale(float scale) noexcept { g_ImGuiDpiScale = std::max(scale, 0.01f); }
    void  setImGuiUserScale(float scale) noexcept { g_ImGuiUserScale = std::max(scale, 0.01f); }
    float imguiDpiScale() noexcept { return g_ImGuiDpiScale * g_ImGuiUserScale; }
} // namespace vultra
