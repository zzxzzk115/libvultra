#include "editor_app/ui/editor_window_manager.hpp"

namespace vultra_app
{
    void EditorWindowManager::draw(EditorContext& ctx)
    {
        for (auto& window : m_Windows)
        {
            if (window->open())
                window->draw(ctx);
        }
    }
} // namespace vultra_app
