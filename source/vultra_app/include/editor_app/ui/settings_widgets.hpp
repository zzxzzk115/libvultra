#pragma once

#include "app_state.hpp"

namespace vultra_app::ui
{
    bool settingsNavItem(const char* label, bool selected);
    void drawSettingsSectionHeader(const char* label);

    // Canonical label-left / control-right property row. Draws the aligned label,
    // moves to labelWidth, and sets the next item to fill the remaining width.
    // Pair with endPropertyRow().
    bool beginPropertyRow(const char* label, float labelWidth = 160.0f);
    void endPropertyRow();

    // Settings-dialog flavor of a property row (narrower default label width).
    bool beginSettingsRow(const char* label, float labelWidth = 150.0f);
    void endSettingsRow();
    void drawInfoRegion(const char* text);
    void alignSettingsButtonGroup(int buttonCount, float buttonWidth = 82.0f);
    void centerNextModalInCurrentWindow();
    void applyEditorSettingsRuntime(const AppState::EditorSettings& settings);

    // Reusable editors for the project classification tables, shared by the Project Settings
    // "Tags & Layers" page and the inspector's tag/layer quick-edit popups. Each renders the
    // add/rename/delete UI directly into the current window and returns true when the backing
    // AppState changed this frame. "Untagged" and the built-in render layers (0 = Default,
    // 5 = UI) are read-only.
    bool drawTagListEditor(AppState& state);
    bool drawRenderLayerListEditor(AppState& state);

    // Persist the project's tag list and render-layer name table to its .vproject, leaving every
    // other project field untouched. Returns false when there is no loadable current project.
    bool persistTagsAndLayers(const AppState& state);
} // namespace vultra_app::ui
