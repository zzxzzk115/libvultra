#include "editor_app/ui/windows/scripted_window.hpp"

#include "editor_app/editor_context.hpp"

#include <vultra/core/base/common_context.hpp>
#include <vultra/core/i18n/i18n.hpp>

#include <imgui.h>

#include <utility>

namespace vultra_app
{
    ScriptedEditorWindow::ScriptedEditorWindow(vultra::ScriptedEditorPanelDesc desc) :
        EditorWindow(desc.id, desc.icon, desc.titleKey), m_Desc(std::move(desc))
    {
        m_LiteralTitle = m_Desc.title.empty() ? m_Desc.id : m_Desc.title;
        m_Open         = m_Desc.defaultOpen;
        refreshLocalization();
    }

    void ScriptedEditorWindow::refreshLocalization()
    {
        // i18n key wins; otherwise show the literal title. Keep "###id" stable
        // so docking layout survives language switches and panel re-adds.
        const std::string label = m_TitleKey.empty() ? m_LiteralTitle : std::string {vultra::tr(m_TitleKey)};
        m_DisplayName           = m_Icon.empty() ? label : m_Icon + "  " + label;
        m_Title                 = m_DisplayName + "###" + m_Name;
    }

    void ScriptedEditorWindow::draw(EditorContext& ctx)
    {
        if (!m_Open)
            return;

        if (ImGui::Begin(title().c_str(), &m_Open))
        {
            if (m_Desc.onDraw.valid())
            {
                // The Lua panel draws with ImGui.* widgets into this window.
                // A scripting error must not tear down the editor: log once
                // per failing frame and keep the window alive.
                sol::protected_function_result r = m_Desc.onDraw();
                if (!r.valid())
                {
                    const sol::error err = r;
                    VULTRA_CORE_ERROR("[EditorExtensions] panel '{}' onDraw error: {}", m_Desc.id, err.what());
                }
            }
        }
        ImGui::End();
    }

    void ScriptedEditorWindow::onClosed(EditorContext& /*ctx*/)
    {
        if (m_Desc.onClosed.valid())
        {
            sol::protected_function_result r = m_Desc.onClosed();
            if (!r.valid())
            {
                const sol::error err = r;
                VULTRA_CORE_ERROR("[EditorExtensions] panel '{}' onClosed error: {}", m_Desc.id, err.what());
            }
        }
    }
} // namespace vultra_app
