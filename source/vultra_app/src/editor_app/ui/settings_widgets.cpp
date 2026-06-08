#include "editor_app/ui/settings_widgets.hpp"

#include <vultra/function/imgui/imgui_dpi.hpp>
#include <vultra/function/imgui/imgui_theme.hpp>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace vultra_app::ui
{
    namespace
    {
        ImVec4 toImVec4(const glm::vec4& color) { return {color.x, color.y, color.z, color.w}; }

        void applyThemePalette(const AppState::EditorSettings& settings)
        {
            namespace theme = vultra::imgui_theme;

            if (settings.theme == "Graphite")
                theme::setPreset(theme::Preset::Graphite);
            else if (settings.theme == "Light")
                theme::setPreset(theme::Preset::Light);
            else if (settings.theme == "Custom")
            {
                theme::setPalette(theme::makeCustomPalette(toImVec4(settings.customThemeBackground),
                                                           toImVec4(settings.customThemePanel),
                                                           toImVec4(settings.customThemeText),
                                                           toImVec4(settings.customThemeAccent)));
            }
            else
                theme::setPreset(theme::Preset::Dark);
        }

        void applyCurrentThemeToImGuiStyle()
        {
            auto& c         = ImGui::GetStyle().Colors;
            namespace theme = vultra::imgui_theme;

            c[ImGuiCol_Text]                  = theme::text();
            c[ImGuiCol_TextDisabled]          = theme::textMuted();
            c[ImGuiCol_WindowBg]              = theme::backgroundTransparent(0.985f);
            c[ImGuiCol_ChildBg]               = theme::backgroundTransparent(0.965f);
            c[ImGuiCol_PopupBg]               = theme::backgroundTransparent(0.985f);
            c[ImGuiCol_Border]                = theme::withAlpha(theme::border(), 0.88f);
            c[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
            c[ImGuiCol_FrameBg]               = theme::withAlpha(theme::frame(), 0.96f);
            c[ImGuiCol_FrameBgHovered]        = theme::frameHovered();
            c[ImGuiCol_FrameBgActive]         = theme::frameActive();
            c[ImGuiCol_TitleBg]               = theme::backgroundDeep();
            c[ImGuiCol_TitleBgActive]         = theme::panel();
            c[ImGuiCol_TitleBgCollapsed]      = theme::withAlpha(theme::backgroundDeeper(), 0.95f);
            c[ImGuiCol_MenuBarBg]             = theme::backgroundDeep();
            c[ImGuiCol_ScrollbarBg]           = theme::withAlpha(theme::backgroundDeeper(), 0.70f);
            c[ImGuiCol_ScrollbarGrab]         = theme::withAlpha(theme::border(), 0.95f);
            c[ImGuiCol_ScrollbarGrabHovered]  = theme::frameHovered();
            c[ImGuiCol_ScrollbarGrabActive]   = theme::frameActive();
            c[ImGuiCol_CheckMark]             = theme::accent();
            c[ImGuiCol_SliderGrab]            = theme::accentTransparent(0.90f);
            c[ImGuiCol_SliderGrabActive]      = theme::accent();
            c[ImGuiCol_Button]                = theme::buttonTransparent(0.96f);
            c[ImGuiCol_ButtonHovered]         = theme::buttonHovered();
            c[ImGuiCol_ButtonActive]          = theme::accentButton();
            c[ImGuiCol_Header]                = theme::withAlpha(theme::header(), 0.88f);
            c[ImGuiCol_HeaderHovered]         = theme::withAlpha(theme::headerHovered(), 0.96f);
            c[ImGuiCol_HeaderActive]          = theme::headerActive();
            c[ImGuiCol_Separator]             = theme::withAlpha(theme::separator(), 0.95f);
            c[ImGuiCol_SeparatorHovered]      = theme::accentButtonHovered();
            c[ImGuiCol_SeparatorActive]       = theme::accent();
            c[ImGuiCol_ResizeGrip]            = theme::accentTransparent(0.30f);
            c[ImGuiCol_ResizeGripHovered]     = theme::accentTransparent(0.65f);
            c[ImGuiCol_ResizeGripActive]      = theme::accentTransparent(0.95f);
            c[ImGuiCol_Tab]                   = theme::panel();
            c[ImGuiCol_TabHovered]            = theme::buttonHovered();
            c[ImGuiCol_TabActive]             = theme::frameHovered();
            c[ImGuiCol_TabUnfocused]          = theme::background();
            c[ImGuiCol_TabUnfocusedActive]    = theme::frameHovered();
            c[ImGuiCol_TableHeaderBg]         = theme::panel();
            c[ImGuiCol_TableBorderStrong]     = theme::border();
            c[ImGuiCol_TableBorderLight]      = theme::separator();
            c[ImGuiCol_TableRowBg]            = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
            c[ImGuiCol_TableRowBgAlt]         = theme::withAlpha(theme::text(), 0.025f);
            c[ImGuiCol_PlotLines]             = theme::accentTransparent(0.82f);
            c[ImGuiCol_PlotLinesHovered]      = theme::accent();
            c[ImGuiCol_PlotHistogram]         = theme::accentTransparent(0.76f);
            c[ImGuiCol_PlotHistogramHovered]  = theme::accent();
            c[ImGuiCol_TextSelectedBg]        = theme::accentTransparent(0.55f);
            c[ImGuiCol_DragDropTarget]        = theme::accentTransparent(0.90f);
            c[ImGuiCol_NavHighlight]          = theme::accentTransparent(0.90f);
            c[ImGuiCol_NavWindowingHighlight] = theme::accentTransparent(0.72f);
            c[ImGuiCol_NavWindowingDimBg]     = theme::withAlpha(theme::backgroundDeeper(), 0.35f);
            c[ImGuiCol_ModalWindowDimBg]      = theme::dim();
#ifdef IMGUI_HAS_DOCK
            c[ImGuiCol_DockingPreview] = theme::accentTransparent(0.62f);
            c[ImGuiCol_DockingEmptyBg] = theme::backgroundDeep();
#endif
        }
    } // namespace

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

    bool beginPropertyRow(const char* label, const float labelWidth)
    {
        ImGui::PushID(label);
        ImGui::AlignTextToFramePadding();

        // Keep the label inside its column so a long label never overflows into (and under) the
        // value control. If it doesn't fit, ellipsize it and show the full text on hover.
        const float avail = labelWidth - ImGui::GetStyle().ItemSpacing.x;
        if (avail <= 0.0f || ImGui::CalcTextSize(label).x <= avail)
        {
            ImGui::TextUnformatted(label);
        }
        else
        {
            constexpr const char* kEllipsis = "...";
            const float           ellipsisW = ImGui::CalcTextSize(kEllipsis).x;
            std::string           clipped {label};
            while (!clipped.empty() && ImGui::CalcTextSize(clipped.c_str()).x + ellipsisW > avail)
                clipped.pop_back();
            clipped += kEllipsis;
            ImGui::TextUnformatted(clipped.c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", label);
        }

        ImGui::SameLine(labelWidth);
        ImGui::SetNextItemWidth(-1.0f);
        return true;
    }

    void endPropertyRow() { ImGui::PopID(); }

    bool beginSettingsRow(const char* label, const float labelWidth)
    {
        return beginPropertyRow(label, labelWidth);
    }

    void endSettingsRow() { endPropertyRow(); }

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
        const float width =
            static_cast<float>(buttonCount) * buttonWidth + static_cast<float>(std::max(buttonCount - 1, 0)) * spacing;
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

        auto& io           = ImGui::GetIO();
        io.FontGlobalScale = textScale;
        applyThemePalette(settings);
        applyCurrentThemeToImGuiStyle();

        if (std::abs(applicationScale - appliedApplicationScale) > 0.001f)
        {
            ImGui::GetStyle().ScaleAllSizes(applicationScale / appliedApplicationScale);
            appliedApplicationScale = applicationScale;
        }

        // Keep ui::dp() in step with the style: the OS DPI factor is set once by ImGuiSystem; this is
        // the user "Application Scale" multiplier on top, so hardcoded px stay proportional to widgets.
        vultra::setImGuiUserScale(applicationScale);
    }
} // namespace vultra_app::ui
