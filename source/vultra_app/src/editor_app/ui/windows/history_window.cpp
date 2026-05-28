#include "editor_app/ui/windows/history_window.hpp"

#include "editor_app/editor_history.hpp"

#include <IconsMaterialDesignIcons.h>
#include <imgui.h>

namespace vultra_app
{
    HistoryWindow::HistoryWindow() : EditorWindow("History", ICON_MDI_BACKUP_RESTORE) {}

    void HistoryWindow::draw(EditorContext& ctx)
    {
        if (!ImGui::Begin(title().c_str(), &m_Open))
        {
            ImGui::End();
            return;
        }

        auto* history = ctx.history;
        if (!history)
        {
            ImGui::TextDisabled("History is unavailable.");
            ImGui::End();
            return;
        }

        const bool canUndo = history->canUndo();
        const bool canRedo = history->canRedo();
        if (!canUndo)
            ImGui::BeginDisabled();
        if (ImGui::SmallButton(ICON_MDI_UNDO))
            history->undo(ctx);
        if (!canUndo)
            ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Undo (Ctrl+Z)");

        ImGui::SameLine();
        if (!canRedo)
            ImGui::BeginDisabled();
        if (ImGui::SmallButton(ICON_MDI_REDO))
            history->redo(ctx);
        if (!canRedo)
            ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Redo (Ctrl+Shift+Z)");

        ImGui::Separator();

        const auto& entries = history->entries();
        if (entries.empty())
        {
            ImGui::TextDisabled("No scene history yet.");
            ImGui::End();
            return;
        }

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(entries.size()));
        while (clipper.Step())
        {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
            {
                const auto  index    = static_cast<std::size_t>(row);
                const auto& entry    = entries[index];
                const bool  selected = index == history->currentIndex();
                const auto  label    = entry.dirty ? entry.label + " *" : entry.label;
                if (ImGui::Selectable(label.c_str(), selected))
                    history->jumpTo(ctx, index);
            }
        }

        ImGui::End();
    }
} // namespace vultra_app
