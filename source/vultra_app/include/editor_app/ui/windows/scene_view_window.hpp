#pragma once

#include "editor_app/ui/editor_window.hpp"

namespace vultra_app
{
    class SceneViewWindow final : public EditorWindow
    {
    public:
        SceneViewWindow();

        void draw(EditorContext& ctx) override;
    };
} // namespace vultra_app
