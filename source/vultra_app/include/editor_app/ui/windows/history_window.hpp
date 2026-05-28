#pragma once

#include "editor_app/ui/editor_window.hpp"

namespace vultra_app
{
    class HistoryWindow final : public EditorWindow
    {
    public:
        HistoryWindow();

        void draw(EditorContext& ctx) override;
    };
} // namespace vultra_app
