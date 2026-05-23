#pragma once

#include "editor_app/ui/editor_window.hpp"

#include <vultra/core/rhi/structs/extent2d.hpp>
#include <vultra/core/rhi/texture.hpp>
#include <vultra/function/services/imgui_service.hpp>

#include <optional>
#include <vector>

namespace vultra_app
{
    class GameViewWindow final : public EditorWindow
    {
    public:
        GameViewWindow();

        void draw(EditorContext& ctx) override;
        void onClosed(EditorContext& ctx) override;
        void onDestroy(EditorContext& ctx) override;

    private:
        struct RenderTargetSlot
        {
            std::optional<vultra::rhi::Texture> texture;
            vultra::rhi::Extent2D               extent {};
            vultra::IImGuiService::TextureID    textureId {};
            uint64_t                            frameCreated {0};
            uint64_t                            releaseFrame {0};
        };

        void drawToolbar(EditorContext& ctx);
        void drawMetricsOverlay(EditorContext& ctx, const ImVec2& imageMin, const ImVec2& imageMax);
        ImVec2 computeRenderSize(const ImVec2& avail) const;
        float  computeFitZoom(const ImVec2& avail, const ImVec2& renderSize) const;
        void ensureRenderTarget(EditorContext& ctx, uint32_t width, uint32_t height);
        void promotePendingRenderTarget(EditorContext& ctx);
        void retireRenderTarget(RenderTargetSlot& slot);
        void collectRetiredRenderTargets(EditorContext& ctx);
        void releaseRenderTarget(EditorContext& ctx);
        void resetRenderTargetsForProject(EditorContext& ctx);

        float m_UserZoom {1.0f};
        float m_MinZoom {1.0f};
        int   m_SelectedResolution {0};
        ImVec2 m_LastViewportAvail {1.0f, 1.0f};

        RenderTargetSlot              m_ActiveRenderTarget;
        RenderTargetSlot              m_PendingRenderTarget;
        std::vector<RenderTargetSlot> m_RetiredRenderTargets;
        uint64_t                      m_ProjectGeneration {0};
    };
} // namespace vultra_app
