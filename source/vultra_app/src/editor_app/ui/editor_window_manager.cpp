#include "editor_app/ui/editor_window_manager.hpp"

#include <imgui.h>

namespace vultra_app
{
    void EditorWindowManager::tick(EditorContext& ctx)
    {
        for (auto& window : m_Windows)
        {
            if (window->open())
                window->tick(ctx);
        }
    }

    void EditorWindowManager::draw(EditorContext& ctx)
    {
        if (m_WasOpen.size() != m_Windows.size())
        {
            m_WasOpen.resize(m_Windows.size(), false);
            for (std::size_t i = 0; i < m_Windows.size(); ++i)
                m_WasOpen[i] = m_Windows[i]->open();
        }

        for (std::size_t i = 0; i < m_Windows.size(); ++i)
        {
            auto& window = m_Windows[i];
            bool  focusRequested = false;
            if (ctx.state.codeEditorOpenRequested && window->name() == "Code Editor")
            {
                window->open() = true;
                focusRequested = true;
                ctx.state.codeEditorOpenRequested = false;
            }
            if (ctx.state.profilerWindowOpenRequested && window->name() == "Profiler")
            {
                window->open() = true;
                focusRequested = true;
                ctx.state.profilerWindowOpenRequested = false;
            }
            if (ctx.state.frameDebuggerWindowOpenRequested && window->name() == "Frame Debugger")
            {
                window->open() = true;
                focusRequested = true;
                ctx.state.frameDebuggerWindowOpenRequested = false;
            }
            if (window->open())
            {
                if (focusRequested)
                    ImGui::SetNextWindowFocus();
                window->draw(ctx);
            }
            else if (m_WasOpen[i])
            {
                window->onClosed(ctx);
            }
            m_WasOpen[i] = window->open();
        }
    }

    void EditorWindowManager::destroy(EditorContext& ctx)
    {
        for (auto& window : m_Windows)
            window->onDestroy(ctx);
        m_Windows.clear();
        m_WasOpen.clear();
    }
} // namespace vultra_app
