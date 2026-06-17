#pragma once

#include "editor_app/editor_context.hpp"
#include "editor_app/ui/editor_window.hpp"

#include <functional>
#include <memory>
#include <vector>

namespace vultra_app
{
    struct EditorTopBarActions
    {
        std::function<void(EditorContext&)> newBlankScene;
        std::function<void(EditorContext&)> saveScene;
        std::function<void(EditorContext&)> buildAndRun;
        std::function<void(EditorContext&)> persistExportSettings;
        std::function<void(EditorContext&)> backToLauncher;
        std::function<void(EditorContext&)> resetLayout;
        std::function<void(EditorContext&)> showAbout;
    };

    void drawEditorTopBar(EditorContext&                                      ctx,
                          const std::vector<std::unique_ptr<EditorWindow>>& windows,
                          const EditorTopBarActions&                         actions);
} // namespace vultra_app
