# Editor Themes

## Goal

Add built-in editor themes and a custom theme option that can be switched at
runtime from Editor Settings.

## Scope

- Add a theme palette layer behind `vultra::imgui_theme`.
- Keep existing UI code calling `imgui_theme::*()` unchanged.
- Add built-in presets and a session-local custom palette.
- Expose theme selection in `Editor Settings > Appearance`.
- Apply theme changes dynamically without restarting the editor.
- Persist editor settings to `.vultra/editor_settings.json`.

## Verification

- `xmake build -y vultra-app`
