#pragma once

#include "editor_app/ui/editor_window.hpp"

#include <vultra/function/services/editor_extension_service.hpp>

#include <string>

namespace vultra_app
{
    // An EditorWindow backed by a Lua-registered panel (Editor.registerPanel).
    // The C++ side owns the Begin/End and the open flag; the Lua onDraw runs
    // inside the frame and draws contents with the ImGui.* bindings. Created by
    // EditorApp when it drains IEditorExtensionService::takeAddedPanels().
    class ScriptedEditorWindow final : public EditorWindow
    {
    public:
        explicit ScriptedEditorWindow(vultra::ScriptedEditorPanelDesc desc);

        void draw(EditorContext& ctx) override;
        void onClosed(EditorContext& ctx) override;
        void refreshLocalization() override;

        [[nodiscard]] const std::string& panelId() const { return m_Desc.id; }

    private:
        vultra::ScriptedEditorPanelDesc m_Desc;
        std::string                     m_LiteralTitle; // used when titleKey is empty
    };
} // namespace vultra_app
