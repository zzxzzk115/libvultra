#pragma once

#include "editor_app/ui/editor_window.hpp"

#include <vultra/core/rhi/structs/extent2d.hpp>
#include <vultra/core/rhi/texture.hpp>
#include <vultra/function/services/imgui_service.hpp>

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

        enum class Tool
        {
            Select,
            Move,
            Rotate,
            Scale,
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

        void drawToolbar(const ImVec2& viewportMin);
        void drawGameViewOverlay(EditorContext& ctx, const ImVec2& viewportMin, const ImVec2& viewportMax);
        void ensureRenderTarget(EditorContext& ctx, uint32_t width, uint32_t height);
        void ensureGameOverlayRenderTarget(EditorContext& ctx, uint32_t width, uint32_t height);
        void promotePendingRenderTarget(EditorContext& ctx);
        void promotePendingGameOverlayRenderTarget(EditorContext& ctx);
        void retireRenderTarget(RenderTargetSlot& slot);
        void retireGameOverlayRenderTarget(RenderTargetSlot& slot);
        void collectRetiredRenderTargets(EditorContext& ctx);
        void collectRetiredGameOverlayRenderTargets(EditorContext& ctx);
        void releaseRenderTarget(EditorContext& ctx);
        void releaseGameOverlayRenderTarget(EditorContext& ctx);

        Tool m_Tool {Tool::Select};
        bool m_ShowGrid {false};

        RenderTargetSlot              m_ActiveRenderTarget;
        RenderTargetSlot              m_PendingRenderTarget;
        std::vector<RenderTargetSlot> m_RetiredRenderTargets;
        RenderTargetSlot              m_GameOverlayActiveRenderTarget;
        RenderTargetSlot              m_GameOverlayPendingRenderTarget;
        std::vector<RenderTargetSlot> m_GameOverlayRetiredRenderTargets;

        glm::vec3 m_CameraPosition {0.0f, 1.6f, 4.0f};
        float     m_CameraYaw {-90.0f};
        float     m_CameraPitch {-15.0f};
        float     m_CameraFovY {60.0f};
        float     m_GameOverlayZoom {1.0f};
    };
} // namespace vultra_app
