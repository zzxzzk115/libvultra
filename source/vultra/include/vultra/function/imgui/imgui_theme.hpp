#pragma once

#include <imgui.h>

namespace vultra::imgui_theme
{
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
        return {0.86f, 0.90f, 0.95f, 1.0f};
    }

    inline ImVec4 textMuted()
    {
        return {0.48f, 0.54f, 0.61f, 1.0f};
    }

    inline ImVec4 textSoft()
    {
        return {0.72f, 0.80f, 0.92f, 1.0f};
    }

    inline ImVec4 background()
    {
        return {0.050f, 0.063f, 0.080f, 1.0f};
    }

    inline ImVec4 backgroundTransparent(const float alpha)
    {
        auto color = background();
        color.w    = alpha;
        return color;
    }

    inline ImVec4 backgroundDeep()
    {
        return {0.040f, 0.050f, 0.064f, 1.0f};
    }

    inline ImVec4 backgroundDeeper()
    {
        return {0.035f, 0.043f, 0.055f, 1.0f};
    }

    inline ImVec4 panel()
    {
        return {0.058f, 0.072f, 0.092f, 1.0f};
    }

    inline ImVec4 frame()
    {
        return {0.040f, 0.052f, 0.068f, 1.0f};
    }

    inline ImVec4 frameHovered()
    {
        return {0.075f, 0.105f, 0.138f, 1.0f};
    }

    inline ImVec4 frameActive()
    {
        return {0.095f, 0.145f, 0.190f, 1.0f};
    }

    inline ImVec4 button()
    {
        return {0.075f, 0.105f, 0.138f, 1.0f};
    }

    inline ImVec4 buttonTransparent(const float alpha)
    {
        auto color = button();
        color.w    = alpha;
        return color;
    }

    inline ImVec4 buttonHovered()
    {
        return {0.100f, 0.145f, 0.190f, 1.0f};
    }

    inline ImVec4 accent()
    {
        return {0.32f, 0.74f, 1.00f, 1.0f};
    }

    inline ImVec4 accentTransparent(const float alpha)
    {
        return withAlpha(accent(), alpha);
    }

    inline ImVec4 accentButton()
    {
        return {0.075f, 0.310f, 0.545f, 1.0f};
    }

    inline ImVec4 accentButtonHovered()
    {
        return {0.115f, 0.390f, 0.690f, 1.0f};
    }

    inline ImVec4 accentButtonActive()
    {
        return {0.065f, 0.275f, 0.500f, 1.0f};
    }

    inline ImVec4 header()
    {
        return {0.075f, 0.105f, 0.138f, 1.0f};
    }

    inline ImVec4 headerHovered()
    {
        return {0.100f, 0.145f, 0.190f, 1.0f};
    }

    inline ImVec4 headerActive()
    {
        return accentButtonActive();
    }

    inline ImVec4 border()
    {
        return {0.140f, 0.180f, 0.230f, 1.0f};
    }

    inline ImVec4 separator()
    {
        return {0.130f, 0.165f, 0.205f, 1.0f};
    }

    inline ImVec4 dim()
    {
        return {0.01f, 0.015f, 0.020f, 0.68f};
    }

    inline ImVec4 destructiveHovered()
    {
        return {0.570f, 0.120f, 0.120f, 1.0f};
    }

    inline ImVec4 destructiveActive()
    {
        return {0.720f, 0.140f, 0.140f, 1.0f};
    }

    inline ImVec4 success()
    {
        return {0.145f, 0.430f, 0.145f, 1.0f};
    }

    inline ImVec4 successHovered()
    {
        return {0.205f, 0.610f, 0.180f, 1.0f};
    }

    inline ImVec4 successActive()
    {
        return {0.105f, 0.360f, 0.115f, 1.0f};
    }
} // namespace vultra::imgui_theme
