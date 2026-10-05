#pragma once

#include "research_workspace.hpp"

#include <vultra/ui/editor_gui_frame.hpp>

namespace vultra
{
    class EditorGui;
    class EnvironmentNode;

    // Editor-only selection. Object IDs are resolved each frame; no node/resource pointers are retained.
    class SceneInspector
    {
    public:
        explicit SceneInspector(EditorGui& gui);
        void drawTree(EditorGuiFrame& ui, const SceneTree& tree);
        void drawProperties(EditorGuiFrame& ui, ResearchWorkspace& workspace);
        // HDR replacement is staged outside UI construction after the previous GPU/GUI frame completes.
        void applyPending(ResearchWorkspace& workspace);

    private:
        struct EnvironmentEdit
        {
            ObjectId root {};
            ObjectId node {};
            AssetId  radiance {};
        };

        void drawTransform(EditorGuiFrame& ui, Node& node);
        void drawMesh(EditorGuiFrame& ui, ResearchWorkspace& workspace, MeshInstanceNode& mesh);
        void drawEnvironment(EditorGuiFrame& ui, ResearchWorkspace& workspace, EnvironmentNode& environment);

        EditorGui&                     m_Gui;
        ObjectId                       m_Selected {};
        std::optional<EnvironmentEdit> m_EnvironmentEdit;
    };
} // namespace vultra
