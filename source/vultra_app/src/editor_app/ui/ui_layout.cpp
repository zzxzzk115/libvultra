#include "editor_app/ui/ui_layout.hpp"

#include <vultra/function/imgui/imgui_dpi.hpp>

#include <imgui.h>

#include <algorithm>
#include <string>

namespace vultra_app::ui
{
    float adaptiveLabelWidth(const float availablePx, const float preferredLabelPx)
    {
        const float spacing    = ImGui::GetStyle().ItemSpacing.x;
        const float minLabel   = vultra::ui::dp(layout::kMinLabelWidthDp);
        const float minControl = vultra::ui::dp(layout::kMinControlWidthDp);

        float label = preferredLabelPx;
        // Steal width from the label (down to its own minimum) so the control stays usable.
        if (availablePx - label - spacing < minControl)
            label = std::max(minLabel, availablePx - minControl - spacing);
        // Degenerate (tiny) panels: never claim more than the row has, but keep the label visible.
        label = std::min(label, std::max(minLabel, availablePx - spacing));
        return std::max(0.0f, label);
    }

    void labelEllipsized(const char* label, const float widthPx)
    {
        if (label == nullptr)
            return;
        if (widthPx <= 0.0f || ImGui::CalcTextSize(label).x <= widthPx)
        {
            ImGui::TextUnformatted(label);
            return;
        }

        constexpr const char* kEllipsis  = "...";
        const float           ellipsisW  = ImGui::CalcTextSize(kEllipsis).x;
        std::string           clipped {label};
        while (!clipped.empty() && ImGui::CalcTextSize(clipped.c_str()).x + ellipsisW > widthPx)
            clipped.pop_back();
        clipped += kEllipsis;
        ImGui::TextUnformatted(clipped.c_str());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", label);
    }

    float checkboxWidth(const char* label)
    {
        // Box (a square of frame height) + inner spacing + label text.
        const float text = (label != nullptr && label[0] != '\0') ? ImGui::CalcTextSize(label).x : 0.0f;
        return ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x + text;
    }

    float buttonWidth(const char* label)
    {
        const float text = (label != nullptr) ? ImGui::CalcTextSize(label).x : 0.0f;
        return text + ImGui::GetStyle().FramePadding.x * 2.0f;
    }

    InlineFlow::InlineFlow() : m_RightEdgeX {ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x} {}

    void InlineFlow::next(const float itemWidthPx)
    {
        if (!m_First)
        {
            const float predicted = ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + itemWidthPx;
            if (predicted <= m_RightEdgeX)
                ImGui::SameLine();
            // else: fall through to a fresh line (default cursor advance).
        }
        m_First = false;
    }
} // namespace vultra_app::ui
