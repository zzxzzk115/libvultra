#pragma once

#include "vultra/core/base/api.hpp"

namespace vultra
{
    // The OS display-DPI component of the UI scale. Set once by ImGuiSystem from the window's display
    // scale; already factors out any density the framebuffer carries (macOS Retina).
    VULTRA_API void setImGuiDpiScale(float scale) noexcept;

    // An optional extra user multiplier (e.g. the editor's "Application Scale" setting) that is also
    // applied to the ImGui style; keeping dp() in sync avoids hardcoded px drifting from the style.
    VULTRA_API void setImGuiUserScale(float scale) noexcept;

    // Effective UI scale (1.0 == 100%) = OS DPI scale * user scale. Multiplying a 96-DPI design pixel
    // value by this gives the correct on-screen size on every platform.
    VULTRA_API float imguiDpiScale() noexcept;

    namespace ui
    {
        // "Literal unit": convert a hardcoded design-pixel constant to a DPI-scaled size so it matches
        // the rest of the UI (which ImGui scales via FontScaleDpi/ScaleAllSizes). ImGui does NOT scale
        // raw px literals in user code, so wrap them: e.g. ImVec2(ui::dp(174.0f), 0.0f), ui::dp(76.0f).
        [[nodiscard]] inline float dp(float designPx) noexcept { return designPx * imguiDpiScale(); }
    } // namespace ui
} // namespace vultra
