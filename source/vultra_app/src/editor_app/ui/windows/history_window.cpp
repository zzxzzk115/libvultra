#include "editor_app/ui/windows/history_window.hpp"

#include "editor_app/editor_history.hpp"

#include <vultra/core/i18n/i18n.hpp>

#include <IconsMaterialDesignIcons.h>
#include <imgui.h>

namespace vultra_app
{
    HistoryWindow::HistoryWindow() : EditorWindow("History", ICON_MDI_BACKUP_RESTORE, "window.history") {}

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
            ImGui::TextDisabled("%s", vultra::tr("history.unavailable"));
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
            ImGui::SetTooltip("%s", vultra::tr("history.undo"));

        ImGui::SameLine();
        if (!canRedo)
            ImGui::BeginDisabled();
        if (ImGui::SmallButton(ICON_MDI_REDO))
            history->redo(ctx);
        if (!canRedo)
            ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("%s", vultra::tr("history.redo"));

        ImGui::Separator();

        const auto& entries = history->entries();
        if (entries.empty())
        {
            ImGui::TextDisabled("%s", vultra::tr("history.empty"));
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
                // Entry labels are stored as i18n keys and resolved here at draw time, so a
                // language switch re-translates the whole history on the next frame. tr()
                // returns the key itself on a miss, so dynamic/literal labels pass through.
                const std::string text  = vultra::tr(entry.label.c_str());
                const auto        label = entry.dirty ? text + " *" : text;
                ImGui::PushID(row);
                if (ImGui::Selectable(label.c_str(), selected))
                    history->jumpTo(ctx, index);
                ImGui::PopID();
            }
        }

        ImGui::End();
    }
} // namespace vultra_app
