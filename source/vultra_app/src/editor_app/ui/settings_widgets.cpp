#include "editor_app/ui/settings_widgets.hpp"

#include <vultra/function/imgui/imgui_theme.hpp>

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace vultra_app::ui
{
    bool settingsNavItem(const char* label, const bool selected)
    {
        if (selected)
            ImGui::PushStyleColor(ImGuiCol_Button, vultra::imgui_theme::accentButton());
        const bool pressed = ImGui::Button(label, ImVec2 {-1.0f, 0.0f});
        if (selected)
            ImGui::PopStyleColor();
        return pressed;
    }

    void drawSettingsSectionHeader(const char* label)
    {
        ImGui::TextUnformatted(label);
        ImGui::Separator();
    }

    bool beginSettingsRow(const char* label, const float labelWidth)
    {
        ImGui::PushID(label);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::SameLine(labelWidth);
        ImGui::SetNextItemWidth(-1.0f);
        return true;
    }

    void endSettingsRow()
    {
        ImGui::PopID();
    }

    void drawInfoRegion(const char* text)
    {
        if (!text || text[0] == '\0')
            return;

        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
        ImGui::TextDisabled("%s", text);
        ImGui::PopTextWrapPos();
    }

    void alignSettingsButtonGroup(const int buttonCount, const float buttonWidth)
    {
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float width = static_cast<float>(buttonCount) * buttonWidth +
                            static_cast<float>(std::max(buttonCount - 1, 0)) * spacing;
        const float cursorX = std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - width);
        ImGui::SameLine(cursorX);
    }

    void centerNextModalInCurrentWindow()
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        if (!viewport)
            return;

        const ImVec2 center {
            viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
            viewport->WorkPos.y + viewport->WorkSize.y * 0.5f,
        };
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2 {0.5f, 0.5f});
    }

    void applyEditorSettingsRuntime(const AppState::EditorSettings& settings)
    {
        static float appliedApplicationScale = 1.0f;

        const float applicationScale = std::clamp(settings.applicationScale, 0.75f, 2.0f);
        const float textScale        = std::clamp(settings.textScale, 0.75f, 2.0f);

        auto& io = ImGui::GetIO();
        io.FontGlobalScale = textScale;

        if (std::abs(applicationScale - appliedApplicationScale) > 0.001f)
        {
            ImGui::GetStyle().ScaleAllSizes(applicationScale / appliedApplicationScale);
            appliedApplicationScale = applicationScale;
        }
    }
} // namespace vultra_app::ui
