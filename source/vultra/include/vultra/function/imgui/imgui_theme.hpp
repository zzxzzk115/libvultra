#pragma once

#include <imgui.h>

namespace vultra::imgui_theme
{
    enum class Preset
    {
        Dark,
        Graphite,
        Light,
        Custom,
    };

    struct Palette
    {
        ImVec4 text {0.86f, 0.90f, 0.95f, 1.0f};
        ImVec4 textMuted {0.48f, 0.54f, 0.61f, 1.0f};
        ImVec4 textSoft {0.72f, 0.80f, 0.92f, 1.0f};
        ImVec4 background {0.050f, 0.063f, 0.080f, 1.0f};
        ImVec4 backgroundDeep {0.040f, 0.050f, 0.064f, 1.0f};
        ImVec4 backgroundDeeper {0.035f, 0.043f, 0.055f, 1.0f};
        ImVec4 panel {0.058f, 0.072f, 0.092f, 1.0f};
        ImVec4 frame {0.040f, 0.052f, 0.068f, 1.0f};
        ImVec4 frameHovered {0.075f, 0.105f, 0.138f, 1.0f};
        ImVec4 frameActive {0.095f, 0.145f, 0.190f, 1.0f};
        ImVec4 button {0.075f, 0.105f, 0.138f, 1.0f};
        ImVec4 buttonHovered {0.100f, 0.145f, 0.190f, 1.0f};
        ImVec4 accent {0.32f, 0.74f, 1.00f, 1.0f};
        ImVec4 accentButton {0.075f, 0.310f, 0.545f, 1.0f};
        ImVec4 accentButtonHovered {0.115f, 0.390f, 0.690f, 1.0f};
        ImVec4 accentButtonActive {0.065f, 0.275f, 0.500f, 1.0f};
        ImVec4 header {0.075f, 0.105f, 0.138f, 1.0f};
        ImVec4 headerHovered {0.100f, 0.145f, 0.190f, 1.0f};
        ImVec4 headerActive {0.065f, 0.275f, 0.500f, 1.0f};
        ImVec4 border {0.140f, 0.180f, 0.230f, 1.0f};
        ImVec4 separator {0.130f, 0.165f, 0.205f, 1.0f};
        ImVec4 dim {0.01f, 0.015f, 0.020f, 0.68f};
        ImVec4 destructiveHovered {0.570f, 0.120f, 0.120f, 1.0f};
        ImVec4 destructiveActive {0.720f, 0.140f, 0.140f, 1.0f};
        ImVec4 success {0.145f, 0.430f, 0.145f, 1.0f};
        ImVec4 successHovered {0.205f, 0.610f, 0.180f, 1.0f};
        ImVec4 successActive {0.105f, 0.360f, 0.115f, 1.0f};
    };

    [[nodiscard]] inline Palette makeDarkPalette()
    {
        return {};
    }

    [[nodiscard]] inline Palette makeGraphitePalette()
    {
        Palette p;
        p.text                = {0.88f, 0.89f, 0.90f, 1.0f};
        p.textMuted           = {0.53f, 0.55f, 0.58f, 1.0f};
        p.textSoft            = {0.74f, 0.77f, 0.81f, 1.0f};
        p.background          = {0.070f, 0.072f, 0.076f, 1.0f};
        p.backgroundDeep      = {0.050f, 0.052f, 0.056f, 1.0f};
        p.backgroundDeeper    = {0.038f, 0.040f, 0.044f, 1.0f};
        p.panel               = {0.090f, 0.094f, 0.100f, 1.0f};
        p.frame               = {0.060f, 0.064f, 0.070f, 1.0f};
        p.frameHovered        = {0.115f, 0.122f, 0.132f, 1.0f};
        p.frameActive         = {0.155f, 0.165f, 0.180f, 1.0f};
        p.button              = {0.125f, 0.132f, 0.145f, 1.0f};
        p.buttonHovered       = {0.175f, 0.188f, 0.205f, 1.0f};
        p.accent              = {0.92f, 0.62f, 0.22f, 1.0f};
        p.accentButton        = {0.420f, 0.245f, 0.075f, 1.0f};
        p.accentButtonHovered = {0.560f, 0.340f, 0.115f, 1.0f};
        p.accentButtonActive  = {0.360f, 0.205f, 0.055f, 1.0f};
        p.header              = p.button;
        p.headerHovered       = p.buttonHovered;
        p.headerActive        = p.accentButtonActive;
        p.border              = {0.230f, 0.240f, 0.255f, 1.0f};
        p.separator           = {0.200f, 0.210f, 0.225f, 1.0f};
        return p;
    }

    [[nodiscard]] inline Palette makeLightPalette()
    {
        Palette p;
        p.text                = {0.080f, 0.095f, 0.115f, 1.0f};
        p.textMuted           = {0.380f, 0.425f, 0.480f, 1.0f};
        p.textSoft            = {0.240f, 0.300f, 0.380f, 1.0f};
        p.background          = {0.910f, 0.925f, 0.945f, 1.0f};
        p.backgroundDeep      = {0.820f, 0.850f, 0.885f, 1.0f};
        p.backgroundDeeper    = {0.760f, 0.795f, 0.835f, 1.0f};
        p.panel               = {0.965f, 0.972f, 0.982f, 1.0f};
        p.frame               = {0.840f, 0.865f, 0.895f, 1.0f};
        p.frameHovered        = {0.770f, 0.815f, 0.865f, 1.0f};
        p.frameActive         = {0.700f, 0.765f, 0.840f, 1.0f};
        p.button              = {0.820f, 0.850f, 0.890f, 1.0f};
        p.buttonHovered       = {0.730f, 0.795f, 0.870f, 1.0f};
        p.accent              = {0.055f, 0.390f, 0.720f, 1.0f};
        p.accentButton        = {0.110f, 0.420f, 0.720f, 1.0f};
        p.accentButtonHovered = {0.145f, 0.500f, 0.835f, 1.0f};
        p.accentButtonActive  = {0.075f, 0.330f, 0.610f, 1.0f};
        p.header              = p.button;
        p.headerHovered       = p.buttonHovered;
        p.headerActive        = p.accentButtonActive;
        p.border              = {0.620f, 0.660f, 0.710f, 1.0f};
        p.separator           = {0.655f, 0.695f, 0.745f, 1.0f};
        p.dim                 = {0.40f, 0.45f, 0.52f, 0.32f};
        return p;
    }

    [[nodiscard]] inline Palette makeCustomPalette(const ImVec4 background,
                                                   const ImVec4 panel,
                                                   const ImVec4 text,
                                                   const ImVec4 accent)
    {
        Palette p;
        p.background          = background;
        p.backgroundDeep      = {background.x * 0.78f, background.y * 0.78f, background.z * 0.78f, background.w};
        p.backgroundDeeper    = {background.x * 0.64f, background.y * 0.64f, background.z * 0.64f, background.w};
        p.panel               = panel;
        p.frame               = {panel.x * 0.78f, panel.y * 0.78f, panel.z * 0.78f, panel.w};
        p.frameHovered        = {panel.x * 1.22f, panel.y * 1.22f, panel.z * 1.22f, panel.w};
        p.frameActive         = {panel.x * 1.48f, panel.y * 1.48f, panel.z * 1.48f, panel.w};
        p.button              = panel;
        p.buttonHovered       = p.frameActive;
        p.text                = text;
        p.textMuted           = {text.x * 0.58f, text.y * 0.58f, text.z * 0.58f, text.w};
        p.textSoft            = {text.x * 0.82f, text.y * 0.86f, text.z * 0.92f, text.w};
        p.accent              = accent;
        p.accentButton        = {accent.x * 0.34f, accent.y * 0.48f, accent.z * 0.62f, accent.w};
        p.accentButtonHovered = {accent.x * 0.45f, accent.y * 0.62f, accent.z * 0.78f, accent.w};
        p.accentButtonActive  = {accent.x * 0.28f, accent.y * 0.42f, accent.z * 0.56f, accent.w};
        p.header              = p.button;
        p.headerHovered       = p.buttonHovered;
        p.headerActive        = p.accentButtonActive;
        p.border              = {panel.x * 1.80f, panel.y * 1.80f, panel.z * 1.80f, panel.w};
        p.separator           = p.border;
        return p;
    }

    [[nodiscard]] inline Palette& currentPaletteStorage()
    {
        static Palette palette = makeDarkPalette();
        return palette;
    }

    [[nodiscard]] inline const Palette& currentPalette()
    {
        return currentPaletteStorage();
    }

    inline void setPalette(const Palette& palette)
    {
        currentPaletteStorage() = palette;
    }

    inline void setPreset(const Preset preset)
    {
        switch (preset)
        {
            case Preset::Graphite:
                setPalette(makeGraphitePalette());
                break;
            case Preset::Light:
                setPalette(makeLightPalette());
                break;
            case Preset::Dark:
            case Preset::Custom:
                setPalette(makeDarkPalette());
                break;
        }
    }

    inline ImU32 u32(const ImVec4 color)
    {
        return ImGui::ColorConvertFloat4ToU32(color);
    }

    inline ImVec4 withAlpha(ImVec4 color, const float alpha)
    {
        color.w = alpha;
        return color;
    }

    inline ImVec4 text()
    {
        return currentPalette().text;
    }

    inline ImVec4 textMuted()
    {
        return currentPalette().textMuted;
    }

    inline ImVec4 textSoft()
    {
        return currentPalette().textSoft;
    }

    inline ImVec4 background()
    {
        return currentPalette().background;
    }

    inline ImVec4 backgroundTransparent(const float alpha)
    {
        auto color = background();
        color.w    = alpha;
        return color;
    }

    inline ImVec4 backgroundDeep()
    {
        return currentPalette().backgroundDeep;
    }

    inline ImVec4 backgroundDeeper()
    {
        return currentPalette().backgroundDeeper;
    }

    inline ImVec4 panel()
    {
        return currentPalette().panel;
    }

    inline ImVec4 frame()
    {
        return currentPalette().frame;
    }

    inline ImVec4 frameHovered()
    {
        return currentPalette().frameHovered;
    }

    inline ImVec4 frameActive()
    {
        return currentPalette().frameActive;
    }

    inline ImVec4 button()
    {
        return currentPalette().button;
    }

    inline ImVec4 buttonTransparent(const float alpha)
    {
        auto color = button();
        color.w    = alpha;
        return color;
    }

    inline ImVec4 buttonHovered()
    {
        return currentPalette().buttonHovered;
    }

    inline ImVec4 accent()
    {
        return currentPalette().accent;
    }

    inline ImVec4 accentTransparent(const float alpha)
    {
        return withAlpha(accent(), alpha);
    }

    inline ImVec4 accentButton()
    {
        return currentPalette().accentButton;
    }

    inline ImVec4 accentButtonHovered()
    {
        return currentPalette().accentButtonHovered;
    }

    inline ImVec4 accentButtonActive()
    {
        return currentPalette().accentButtonActive;
    }

    inline ImVec4 header()
    {
        return currentPalette().header;
    }

    inline ImVec4 headerHovered()
    {
        return currentPalette().headerHovered;
    }

    inline ImVec4 headerActive()
    {
        return currentPalette().headerActive;
    }

    inline ImVec4 border()
    {
        return currentPalette().border;
    }

    inline ImVec4 separator()
    {
        return currentPalette().separator;
    }

    inline ImVec4 dim()
    {
        return currentPalette().dim;
    }

    inline ImVec4 destructiveHovered()
    {
        return currentPalette().destructiveHovered;
    }

    inline ImVec4 destructiveActive()
    {
        return currentPalette().destructiveActive;
    }

    inline ImVec4 success()
    {
        return currentPalette().success;
    }

    inline ImVec4 successHovered()
    {
        return currentPalette().successHovered;
    }

    inline ImVec4 successActive()
    {
        return currentPalette().successActive;
    }
} // namespace vultra::imgui_theme
