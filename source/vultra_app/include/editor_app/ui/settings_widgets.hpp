#pragma once

#include "app_state.hpp"

namespace vultra_app::ui
{
    bool settingsNavItem(const char* label, bool selected);
    void drawSettingsSectionHeader(const char* label);

    // Canonical label-left / control-right property row. Draws the aligned label,
    // moves to labelWidth, and sets the next item to fill the remaining width.
    // Pair with endPropertyRow(). See ai/knowledge/editor-ui-style.md.
    bool beginPropertyRow(const char* label, float labelWidth = 160.0f);
    void endPropertyRow();

    // Settings-dialog flavor of a property row (narrower default label width).
    bool beginSettingsRow(const char* label, float labelWidth = 150.0f);
    void endSettingsRow();
    void drawInfoRegion(const char* text);
    void alignSettingsButtonGroup(int buttonCount, float buttonWidth = 82.0f);
    void centerNextModalInCurrentWindow();
    void applyEditorSettingsRuntime(const AppState::EditorSettings& settings);
} // namespace vultra_app::ui
