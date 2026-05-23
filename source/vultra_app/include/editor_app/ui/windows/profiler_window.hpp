#pragma once

#include "editor_app/ui/editor_window.hpp"

namespace vultra_app
{
    class ProfilerWindow final : public EditorWindow
    {
    public:
        ProfilerWindow();

        void draw(EditorContext& ctx) override;

    private:
        bool m_AutoEnableCapture {false};
    };
} // namespace vultra_app
