#pragma once

#include "app_state.hpp"

namespace vultra_app::ui
{
    bool settingsNavItem(const char* label, bool selected);
    void drawSettingsSectionHeader(const char* label);
    bool beginSettingsRow(const char* label, float labelWidth = 150.0f);
    void endSettingsRow();
    void drawInfoRegion(const char* text);
    void alignSettingsButtonGroup(int buttonCount, float buttonWidth = 82.0f);
    void centerNextModalInCurrentWindow();
    void applyEditorSettingsRuntime(const AppState::EditorSettings& settings);
} // namespace vultra_app::ui
