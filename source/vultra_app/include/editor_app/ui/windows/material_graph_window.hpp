#pragma once

#include "editor_app/ui/editor_window.hpp"
#include "editor_app/ui/mesh_selector.hpp"
#include "editor_app/ui/texture_selector.hpp"

#include <vultra/core/rhi/structs/extent2d.hpp>
#include <vultra/core/rhi/texture.hpp>
#include <vultra/function/material_graph/material_graph.hpp>
#include <vultra/function/material_graph/material_graph_compiler.hpp>
#include <vultra/function/material_graph/material_node_registry.hpp>
#include <vultra/function/services/imgui_service.hpp>
#include <vultra/function/world/world.hpp>

#include <entt/entity/fwd.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>
#include <imnodes/imnodes.h>

#include <array>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace vultra_app
{
    class MaterialGraphWindow final : public EditorWindow
    {
    public:
        MaterialGraphWindow();
        ~MaterialGraphWindow() override;

        void draw(EditorContext& ctx) override;
        void onDestroy(EditorContext& ctx) override;

    private:
        struct RenderTargetSlot
        {
            std::optional<vultra::rhi::Texture> texture;
            vultra::rhi::Extent2D               extent {};
            vultra::IImGuiService::TextureID    textureId {};
            uint64_t                            releaseFrame {0};
        };

        struct PinRef
        {
            std::string node;
            std::string pin;
            bool        input {false};
        };

        void ensureLoaded(EditorContext& ctx);
        void consumeOpenRequest(EditorContext& ctx);
        void newGraph(EditorContext& ctx);
        bool loadGraph(EditorContext& ctx, std::string uri);
        bool saveGraph(EditorContext& ctx);
        bool saveThumbnail(EditorContext& ctx, const std::filesystem::path& sourcePath);
        bool compileGraph(EditorContext& ctx);
        void drawToolbar(EditorContext& ctx);
        void drawNodeEditor(EditorContext& ctx);
        void drawInspector(EditorContext& ctx);
        void drawPreview(EditorContext& ctx);
        void drawAddNodePopup(EditorContext& ctx);
        void markDirty(EditorContext& ctx);
        void updatePreviewFocusAnimation();
        bool focusPreviewMesh(EditorContext& ctx, float aspect, bool resetAngle);
        void refreshNodeRegistry(EditorContext& ctx);

        void ensurePreviewWorld(EditorContext& ctx);
        void ensurePreviewRenderTarget(EditorContext& ctx, uint32_t width, uint32_t height);
        void releasePreviewRenderTarget(EditorContext& ctx);
        void collectRetiredPreviewTargets(EditorContext& ctx);

        int nodeId(std::string_view id) const;
        int pinId(std::string_view node, std::string_view pin, bool input);
        int linkId(const vultra::material_graph::Link& link) const;

        vultra::material_graph::Node* findNode(std::string_view id);
        const vultra::material_graph::Node* findNode(std::string_view id) const;
        void ensureNodePorts(vultra::material_graph::Node& node);

        vultra::material_graph::NodeRegistry    m_Registry;
        vultra::material_graph::Graph           m_Graph;
        std::vector<vultra::material_graph::Diagnostic> m_Diagnostics;
        std::unordered_map<int, PinRef>         m_Pins;
        ImNodesEditorContext*                   m_NodeEditor {nullptr};
        std::string                             m_CurrentUri {"res://materials/default.vmatgraph.json"};
        std::string                             m_Status;
        bool                                    m_Loaded {false};
        bool                                    m_Dirty {false};
        bool                                    m_LiveApply {true};
        int                                     m_ContextNode {0};
        uint64_t                                m_NodeRegistryAssetGeneration {std::numeric_limits<uint64_t>::max()};
        uint64_t                                m_LoadedAssetGeneration {0};
        uint64_t                                m_LoadedWriteStamp {0};

        RenderTargetSlot              m_PreviewTarget;
        std::vector<RenderTargetSlot> m_RetiredPreviewTargets;
        ui::TextureSelectorState      m_TextureSelector;
        ui::MeshSelectorState         m_MeshSelector;
        vultra::World                 m_PreviewWorld;
        entt::entity                  m_PreviewSphere {entt::null};
        entt::entity                  m_PreviewLight {entt::null};
        entt::entity                  m_PreviewEnvironment {entt::null};
        vultra::CoreUUID              m_PreviewMesh;
        vultra::CoreUUID              m_LastPreviewMesh;
        vultra::CoreUUID              m_PreviewSkybox;
        glm::vec3                     m_PreviewCameraPosition {0.0f, 0.35f, 3.1f};
        glm::vec3                     m_PreviewFocusStartPosition {0.0f};
        glm::vec3                     m_PreviewFocusTargetPosition {0.0f};
        float                         m_PreviewCameraFovY {45.0f};
        glm::quat                     m_PreviewObjectRotation {1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3                     m_PreviewArcballVector {0.0f, 0.0f, 1.0f};
        glm::vec3                     m_PreviewBoundsCenter {0.0f};
        float                         m_PreviewFitDistance {3.0f};
        float                         m_PreviewDistanceScale {1.0f};
        float                         m_PreviewFocusElapsed {0.0f};
        float                         m_PreviewFocusDuration {0.28f};
        float                         m_PreviewTimeSeconds {0.0f};
        bool                          m_PreviewTimePlaying {false};
        bool                          m_PreviewFocusActive {false};
        bool                          m_PreviewArcballActive {false};
    };
} // namespace vultra_app
