#pragma once

#include "research_workspace.hpp"

#include <vultra/scene/render_nodes.hpp>
#include <vultra/ui/editor_gui_frame.hpp>

#include <string>

namespace vultra
{
    class EditorGui;
    class WorldEnvironment;

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

        void drawTransform(EditorGuiFrame& ui, Node3D& node);
        void drawResourceProperties(EditorGuiFrame& ui, ResearchWorkspace& workspace, Resource& resource);
        void drawMesh(EditorGuiFrame& ui, ResearchWorkspace& workspace, MeshInstance3D& mesh);
        void drawEnvironment(EditorGuiFrame& ui, ResearchWorkspace& workspace, WorldEnvironment& environment);

        EditorGui&                     m_Gui;
        ObjectId                       m_Selected {};
        std::optional<EnvironmentEdit> m_EnvironmentEdit;
        std::string                    m_PropertyError;
    };
} // namespace vultra
