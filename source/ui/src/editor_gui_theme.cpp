// Palettes adapted from VRI-Framework's ui/themes.hpp (MIT, Copyright 2026 Lazy_V).
// Editor themes approximate the corresponding editors; they are not official skins.
#include <vultra/ui/editor_gui_theme.hpp>

#include <imgui.h>

namespace vultra
{
    namespace
    {
        // Shared skin scaffolding: start from stock dark, then repaint the key slots from a
        // small palette (panel gray ramp + one accent) - enough to carry each editor's look.
        struct SkinPalette
        {
            ImVec4 text;
            ImVec4 windowBg; // main panel background
            ImVec4 childBg;  // nested/child panels + menu bar
            ImVec4 fieldBg;  // dark title and tab backgrounds; default frame color
            ImVec4 button;   // raised controls
            ImVec4 hover;    // hovered raised controls
            ImVec4 accent;   // selection / checkmark / active highlight
            ImVec4 border;
            float  rounding;
        };

        void applySkin(const SkinPalette& p)
        {
            ImGui::StyleColorsDark();
            ImGuiStyle& style = ImGui::GetStyle();
            ImVec4*     c     = style.Colors;

            const auto scaled = [](const ImVec4& v, const float f)
            {
                return ImVec4 {v.x * f, v.y * f, v.z * f, v.w};
            };
            const auto alpha = [](const ImVec4& v, const float a)
            {
                return ImVec4 {v.x, v.y, v.z, a};
            };

            c[ImGuiCol_Text]         = p.text;
            c[ImGuiCol_TextDisabled] = alpha(p.text, 0.45f);

            c[ImGuiCol_WindowBg] = p.windowBg;
            c[ImGuiCol_ChildBg]  = p.childBg;
            c[ImGuiCol_PopupBg]  = alpha(p.childBg, 0.98f);
            c[ImGuiCol_Border]   = p.border;

            c[ImGuiCol_FrameBg]        = p.fieldBg;
            c[ImGuiCol_FrameBgHovered] = scaled(p.fieldBg, 1.35f);
            c[ImGuiCol_FrameBgActive]  = scaled(p.fieldBg, 1.6f);

            c[ImGuiCol_TitleBg]          = p.fieldBg;
            c[ImGuiCol_TitleBgActive]    = p.childBg;
            c[ImGuiCol_TitleBgCollapsed] = alpha(p.fieldBg, 0.8f);
            c[ImGuiCol_MenuBarBg]        = p.childBg;

            c[ImGuiCol_ScrollbarBg]          = alpha(p.fieldBg, 0.6f);
            c[ImGuiCol_ScrollbarGrab]        = p.button;
            c[ImGuiCol_ScrollbarGrabHovered] = p.hover;
            c[ImGuiCol_ScrollbarGrabActive]  = p.accent;

            c[ImGuiCol_CheckMark]        = p.accent;
            c[ImGuiCol_SliderGrab]       = p.accent;
            c[ImGuiCol_SliderGrabActive] = scaled(p.accent, 1.2f);

            c[ImGuiCol_Button]        = p.button;
            c[ImGuiCol_ButtonHovered] = p.hover;
            c[ImGuiCol_ButtonActive]  = p.accent;

            c[ImGuiCol_Header]        = alpha(p.accent, 0.55f);
            c[ImGuiCol_HeaderHovered] = alpha(p.accent, 0.75f);
            c[ImGuiCol_HeaderActive]  = p.accent;

            c[ImGuiCol_Separator]        = p.border;
            c[ImGuiCol_SeparatorHovered] = alpha(p.accent, 0.6f);
            c[ImGuiCol_SeparatorActive]  = p.accent;

            c[ImGuiCol_ResizeGrip]        = alpha(p.accent, 0.2f);
            c[ImGuiCol_ResizeGripHovered] = alpha(p.accent, 0.6f);
            c[ImGuiCol_ResizeGripActive]  = p.accent;

            c[ImGuiCol_Tab]               = p.fieldBg;
            c[ImGuiCol_TabHovered]        = p.hover;
            c[ImGuiCol_TabSelected]       = p.windowBg;
            c[ImGuiCol_TabDimmed]         = alpha(p.fieldBg, 0.9f);
            c[ImGuiCol_TabDimmedSelected] = p.childBg;
            c[ImGuiCol_DockingPreview]    = alpha(p.accent, 0.6f);
            c[ImGuiCol_DockingEmptyBg]    = scaled(p.fieldBg, 0.8f);
            c[ImGuiCol_TextSelectedBg]    = alpha(p.accent, 0.4f);
            c[ImGuiCol_NavCursor]         = p.accent;
            c[ImGuiCol_DragDropTarget]    = alpha(p.accent, 0.9f);

            style.WindowRounding    = 0.0f;
            style.ChildRounding     = p.rounding;
            style.FrameRounding     = p.rounding;
            style.PopupRounding     = p.rounding;
            style.GrabRounding      = p.rounding;
            style.TabRounding       = p.rounding;
            style.ScrollbarRounding = p.rounding;
        }
    } // namespace

    void applyEditorGuiTheme(EditorGuiTheme theme)
    {
        auto&            style = ImGui::GetStyle();
        const ImGuiStyle defaults;
        style.WindowRounding    = defaults.WindowRounding;
        style.ChildRounding     = defaults.ChildRounding;
        style.FrameRounding     = defaults.FrameRounding;
        style.PopupRounding     = defaults.PopupRounding;
        style.GrabRounding      = defaults.GrabRounding;
        style.TabRounding       = defaults.TabRounding;
        style.ScrollbarRounding = defaults.ScrollbarRounding;
        switch (theme)
        {
            case EditorGuiTheme::eDark:
                ImGui::StyleColorsDark();
                break;
            case EditorGuiTheme::eLight:
                ImGui::StyleColorsLight();
                break;
            case EditorGuiTheme::eUnity:
                // Unity dark: mid-gray panel ramp, muted steel-blue selection, square-ish corners.
                applySkin({
                    .text     = {0.82f, 0.82f, 0.82f, 1.0f},
                    .windowBg = {0.220f, 0.220f, 0.220f, 1.0f}, // #383838
                    .childBg  = {0.190f, 0.190f, 0.190f, 1.0f}, // #303030
                    .fieldBg  = {0.165f, 0.165f, 0.165f, 1.0f}, // #2a2a2a
                    .button   = {0.345f, 0.345f, 0.345f, 1.0f}, // #585858
                    .hover    = {0.424f, 0.424f, 0.424f, 1.0f}, // #6c6c6c
                    .accent   = {0.173f, 0.365f, 0.529f, 1.0f}, // #2c5d87 selection blue
                    .border   = {0.140f, 0.140f, 0.140f, 1.0f},
                    .rounding = 2.0f,
                });
                break;
            case EditorGuiTheme::eUnreal:
                // Unreal dark: near-black panels, visible framed controls, bright blue accent, round corners.
                applySkin({
                    .text     = {0.78f, 0.78f, 0.78f, 1.0f},
                    .windowBg = {0.082f, 0.082f, 0.082f, 1.0f}, // #151515
                    .childBg  = {0.110f, 0.110f, 0.110f, 1.0f}, // #1c1c1c
                    .fieldBg  = {0.047f, 0.047f, 0.047f, 1.0f}, // #0c0c0c
                    .button   = {0.160f, 0.160f, 0.160f, 1.0f},
                    .hover    = {0.230f, 0.230f, 0.230f, 1.0f},
                    .accent   = {0.000f, 0.440f, 0.800f, 1.0f}, // #0070cc highlight blue
                    .border   = {0.030f, 0.030f, 0.030f, 1.0f},
                    .rounding = 4.0f,
                });
                // Unchecked checkboxes use FrameBg, so they need more contrast than the title and tabs.
                style.Colors[ImGuiCol_FrameBg]        = {0.20f, 0.20f, 0.20f, 1.0f};
                style.Colors[ImGuiCol_FrameBgHovered] = {0.27f, 0.27f, 0.27f, 1.0f};
                style.Colors[ImGuiCol_FrameBgActive]  = {0.34f, 0.34f, 0.34f, 1.0f};
                break;
            case EditorGuiTheme::eGodot:
                // Godot dark: blue-gray panel ramp with a soft periwinkle accent.
                applySkin({
                    .text     = {0.875f, 0.890f, 0.910f, 1.0f}, // #dfe3e8
                    .windowBg = {0.145f, 0.160f, 0.200f, 1.0f}, // #252933
                    .childBg  = {0.130f, 0.145f, 0.180f, 1.0f}, // #21252e
                    .fieldBg  = {0.110f, 0.125f, 0.155f, 1.0f}, // #1c2028
                    .button   = {0.200f, 0.230f, 0.290f, 1.0f}, // #333b4a
                    .hover    = {0.260f, 0.300f, 0.380f, 1.0f},
                    .accent   = {0.410f, 0.610f, 0.910f, 1.0f}, // #699ce8 Godot blue
                    .border   = {0.090f, 0.100f, 0.130f, 1.0f},
                    .rounding = 3.0f,
                });
                break;
        }
        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            style.WindowRounding              = 0;
            style.Colors[ImGuiCol_WindowBg].w = 1;
        }
    }

    const char* editorGuiThemeName(EditorGuiTheme theme)
    {
        switch (theme)
        {
            case EditorGuiTheme::eDark:
                return "Dark (ImGui)";
            case EditorGuiTheme::eLight:
                return "Light (ImGui)";
            case EditorGuiTheme::eUnity:
                return "Unity";
            case EditorGuiTheme::eUnreal:
                return "Unreal";
            case EditorGuiTheme::eGodot:
                return "Godot";
        }
        return "Unknown";
    }
} // namespace vultra
