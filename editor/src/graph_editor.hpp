#pragma once

#include "research_workspace.hpp"

#include <vultra/ui/editor_gui_frame.hpp>

namespace vultra
{
    struct GraphEdit
    {
        bool                     changed = false;
        std::vector<std::string> parameterEdits;
        std::string              preview;
    };

    // Editor-only view of a definition. It never owns GPU resources or executes passes.
    class GraphEditor
    {
    public:
        explicit GraphEditor(const PassCatalog& catalog);
        ~GraphEditor();
        GraphEditor(const GraphEditor&)            = delete;
        GraphEditor& operator=(const GraphEditor&) = delete;

        GraphEdit draw(EditorGuiFrame& ui, ResearchDocument& document, std::string& status);
        void      reset();
        void      saveLayout(ResearchDocument& document) const;
        // Current screen coordinates, also usable by windowless pointer-interaction tests.
        std::optional<ImVec2> portPosition(std::string_view path, bool input = false) const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace vultra
