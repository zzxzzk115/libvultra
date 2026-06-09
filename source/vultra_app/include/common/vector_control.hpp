#pragma once

#include <vultra/function/imgui/imgui_dpi.hpp>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <span>
#include <string>

namespace vultra_app::ui
{
    struct VectorAxisSpec
    {
        const char* label {nullptr};
        float*      value {nullptr};
        float       reset {0.0f};
        ImVec4      color {};
        ImVec4      hovered {};
        ImVec4      active {};
    };

    // Unity-style colored vector editor: a label column followed by N reset-buttoned drag floats.
    // Shared body for the 2/3-component transform controls (position/rotation/scale/anchors). Each axis
    // carries its own button colors so callers can keep their exact palette (explicit or derived).
    inline bool
    drawVectorControl(const char* label, std::span<const VectorAxisSpec> axes, float speed, float minItemWidthDp)
    {
        bool changed = false;
        for (const auto& a : axes)
            if (std::abs(*a.value) < 0.0005f)
                *a.value = 0.0f;

        ImGui::PushID(label);
        ImGui::Columns(2, nullptr, false);
        ImGui::SetColumnWidth(0, vultra::ui::dp(92.0f));
        ImGui::TextUnformatted(label);
        ImGui::NextColumn();

        const float  count      = static_cast<float>(axes.size());
        const float  lineHeight = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.0f;
        const ImVec2 buttonSize {lineHeight + vultra::ui::dp(3.0f), lineHeight};
        const float  itemWidth  = std::max(vultra::ui::dp(minItemWidthDp),
                                          (ImGui::GetContentRegionAvail().x - buttonSize.x * count -
                                           ImGui::GetStyle().ItemSpacing.x * 2.0f * count) /
                                              count);

        for (const auto& a : axes)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, a.color);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, a.hovered);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, a.active);
            if (ImGui::Button(a.label, buttonSize))
            {
                *a.value = a.reset;
                changed  = true;
            }
            ImGui::PopStyleColor(3);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(itemWidth);
            changed |= ImGui::DragFloat((std::string("##") + a.label).c_str(), a.value, speed, 0.0f, 0.0f, "%.3f");
            ImGui::SameLine();
        }
        ImGui::NewLine();

        ImGui::Columns(1);
        ImGui::PopID();
        return changed;
    }
} // namespace vultra_app::ui
