#pragma once

#include "editor_app/ui/editor_window.hpp"

namespace vultra_app
{
    // Lists every vultra::World currently alive in the process, with each world's entity (node)
    // count, component-pool breakdown and an estimated CPU memory footprint. Intended to spot
    // worlds that were not released in time (leaks) -e.g. a lingering staging/preview world after
    // a scene switch.
    class WorldViewerWindow final : public EditorWindow
    {
    public:
        WorldViewerWindow();

        void draw(EditorContext& ctx) override;

    private:
        bool m_ExpandPools {false};
    };
} // namespace vultra_app
