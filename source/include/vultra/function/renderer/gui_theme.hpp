#pragma once

namespace vultra
{
    enum class GuiTheme
    {
        eDark,
        eLight,
        eUnity,
        eUnreal,
        eGodot
    };

    inline constexpr int kGuiThemeCount = 5;
    const char*          guiThemeName(GuiTheme theme);
    // Restyle the current ImGui context, preserving DPI scale and custom spacing.
    void applyGuiTheme(GuiTheme theme);
} // namespace vultra
