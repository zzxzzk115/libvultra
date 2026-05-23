#include "editor_app/ui/editor_window_manager.hpp"

namespace vultra_app
{
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
            if (ctx.state.codeEditorOpenRequested && window->name() == "Code Editor")
            {
                window->open() = true;
                ctx.state.codeEditorOpenRequested = false;
            }
            if (ctx.state.profilerWindowOpenRequested && window->name() == "Profiler")
            {
                window->open() = true;
                ctx.state.profilerWindowOpenRequested = false;
            }
            if (window->open())
            {
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
