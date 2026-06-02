#pragma once

#include "editor_app/ui/editor_window.hpp"

#include "editor_app/selection.hpp"

#include <vultra/core/base/uuid.hpp>
#include <vultra/core/rhi/structs/extent2d.hpp>
#include <vultra/core/rhi/texture.hpp>
#include <vultra/function/services/imgui_service.hpp>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <optional>
#include <vector>

struct ImVec2;

namespace vultra_app
{
    class SceneViewWindow final : public EditorWindow
    {
    public:
        SceneViewWindow();

        void draw(EditorContext& ctx) override;
        void onClosed(EditorContext& ctx) override;
        void onDestroy(EditorContext& ctx) override;
        bool saveSceneThumbnail(EditorContext& ctx, std::string_view sceneUri);

        enum class Tool
        {
            Select,
            Move,
            Rotate,
            Scale,
            Rect,
            Transform,
        };

        enum class ViewMode
        {
            View3D,
            Ui2D,
        };

        enum class CoordinateMode
        {
            Local,
            Global,
        };

        enum class Ui2DDragOperation
        {
            None,
            Move,
            Rotate,
            Scale,
        };

        struct Ui2DDragState
        {
            Ui2DDragOperation operation {Ui2DDragOperation::None};
            vultra::CoreUUID  entityId {};
            glm::vec2         startMouseUi {0.0f};
            glm::vec2         startAnchoredPositionPx {0.0f};
            glm::vec2         startScale {1.0f};
            float             startRotationDegrees {0.0f};
            float             startAngleDegrees {0.0f};
            glm::vec2         scaleAxis {1.0f};
        };

    private:
        struct RenderTargetSlot
        {
            std::optional<vultra::rhi::Texture> texture;
            vultra::rhi::Extent2D               extent {};
            vultra::IImGuiService::TextureID    textureId {};
            uint64_t                            frameCreated {0};
            uint64_t                            releaseFrame {0};
        };

        struct ResizeRequest
        {
            vultra::rhi::Extent2D extent {};
            uint64_t              firstSeenFrame {0};
        };

        void drawToolbar(EditorContext& ctx);
        bool drawViewManipulator(const ImVec2& viewportMin,
                                 const ImVec2& viewportMax,
                                 glm::mat4&    view,
                                 const glm::mat4& projection);
        void drawGameViewOverlay(EditorContext& ctx, const ImVec2& viewportMin, const ImVec2& viewportMax);
        void ensureRenderTarget(EditorContext& ctx, uint32_t width, uint32_t height);
        void ensurePickingRenderTarget(EditorContext& ctx, uint32_t width, uint32_t height);
        void ensureGameOverlayRenderTarget(EditorContext& ctx, uint32_t width, uint32_t height);
        void promotePendingRenderTarget(EditorContext& ctx);
        void promotePendingGameOverlayRenderTarget(EditorContext& ctx);
        void retireRenderTarget(RenderTargetSlot& slot);
        void retirePickingRenderTarget(RenderTargetSlot& slot);
        void retireGameOverlayRenderTarget(RenderTargetSlot& slot);
        void collectRetiredRenderTargets(EditorContext& ctx);
        void collectRetiredPickingRenderTargets();
        void collectRetiredGameOverlayRenderTargets(EditorContext& ctx);
        void releaseRenderTarget(EditorContext& ctx);
        void releasePickingRenderTarget(EditorContext& ctx);
        void releaseGameOverlayRenderTarget(EditorContext& ctx);
        void resetRenderTargetsForProject(EditorContext& ctx);
        void initializeCameraFromPrimaryCamera(EditorContext& ctx);
        void updateFocusAnimation();
        bool focusSelection(EditorContext& ctx, float aspect);

        Tool m_Tool {Tool::Select};
        ViewMode m_ViewMode {ViewMode::View3D};
        CoordinateMode m_CoordinateMode {CoordinateMode::Local};
        bool m_ShowGrid {false};

        RenderTargetSlot              m_ActiveRenderTarget;
        RenderTargetSlot              m_PendingRenderTarget;
        std::vector<RenderTargetSlot> m_RetiredRenderTargets;
        ResizeRequest                 m_RenderTargetResizeRequest;
        RenderTargetSlot              m_PickingRenderTarget;
        std::vector<RenderTargetSlot> m_RetiredPickingRenderTargets;
        RenderTargetSlot              m_GameOverlayActiveRenderTarget;
        RenderTargetSlot              m_GameOverlayPendingRenderTarget;
        std::vector<RenderTargetSlot> m_GameOverlayRetiredRenderTargets;
        uint64_t                      m_GameOverlayLastRenderSignature {0};
        bool                          m_GameOverlayStaticFrameValid {false};
        bool                          m_GameOverlayLastSceneDirty {false};

        glm::vec3 m_CameraPosition {0.0f, 6.5f, 6.5f};
        float     m_CameraYaw {-90.0f};
        float     m_CameraPitch {-45.0f};
        float     m_CameraFovY {60.0f};
        glm::vec3 m_FocusStartPosition {0.0f};
        glm::vec3 m_FocusTargetPosition {0.0f};
        float     m_FocusElapsed {0.0f};
        float     m_FocusDuration {0.35f};
        float     m_GameOverlayZoom {1.0f};
        uint64_t  m_ProjectGeneration {0};
        bool      m_CameraInitializedFromScene {false};
        bool      m_FocusActive {false};
        SelectionCategory m_LastAutoModeSelectionCategory {SelectionCategory::None};
        vultra::CoreUUID  m_LastAutoModeSelectionId {};
        Ui2DDragState     m_Ui2DDrag;
        bool      m_ViewManipulatorDragActive {false};
        glm::vec3 m_ViewManipulatorArcballVector {0.0f, 0.0f, 1.0f};
    };
} // namespace vultra_app
