#pragma once

namespace vultra
{
    enum class EditorGuiTheme
    {
        eDark,
        eLight,
        eUnity,
        eUnreal,
        eGodot
    };

    inline constexpr int kGuiThemeCount = 5;
    const char*          editorGuiThemeName(EditorGuiTheme theme);
    // Restyle the current ImGui context, preserving DPI scale and custom spacing.
    void applyEditorGuiTheme(EditorGuiTheme theme);
} // namespace vultra
