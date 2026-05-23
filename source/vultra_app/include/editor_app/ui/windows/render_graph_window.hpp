#pragma once

#include "editor_app/ui/editor_window.hpp"

#include <vultra/core/rhi/structs/extent2d.hpp>
#include <vultra/core/rhi/texture.hpp>
#include <vultra/function/services/imgui_service.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace vultra_app
{
    class RenderGraphWindow final : public EditorWindow
    {
    public:
        RenderGraphWindow();
        ~RenderGraphWindow() override;

        void draw(EditorContext& ctx) override;

    private:
        enum class Mode
        {
            ePreview,
            eEdit,
        };

        struct GraphEditorState;
        struct RuntimeGraphState;
        struct RenderTargetSlot
        {
            std::optional<vultra::rhi::Texture> texture;
            vultra::rhi::Extent2D               extent {};
            vultra::IImGuiService::TextureID    textureId {};
            uint64_t                            frameCreated {0};
            uint64_t                            releaseFrame {0};
        };

        void drawRuntimeGraph(EditorContext& ctx);
        void drawGraphEditor(EditorContext& ctx);
        void drawGraphEditorAddPopup(EditorContext& ctx);
        void drawGraphEditorCanvas(EditorContext& ctx);
        void drawPipelineEditorCanvas(EditorContext& ctx);
        void onDestroy(EditorContext& ctx) override;
        void drawGameViewOverlay(EditorContext& ctx);
        void ensureOverlayRenderTarget(EditorContext& ctx, uint32_t width, uint32_t height);
        void promotePendingOverlayRenderTarget(EditorContext& ctx);
        void retireOverlayRenderTarget(RenderTargetSlot& slot);
        void collectRetiredOverlayRenderTargets(EditorContext& ctx);
        void releaseOverlayRenderTarget(EditorContext& ctx);
        void resetOverlayRenderTargetForProject(EditorContext& ctx);

        std::unique_ptr<RuntimeGraphState> m_RuntimeGraph;
        std::unique_ptr<GraphEditorState> m_GraphEditor;
        RenderTargetSlot              m_OverlayActiveRenderTarget;
        RenderTargetSlot              m_OverlayPendingRenderTarget;
        std::vector<RenderTargetSlot> m_OverlayRetiredRenderTargets;
        float m_OverlayZoom {1.0f};
        uint64_t m_ProjectGeneration {0};
        Mode m_Mode {Mode::eEdit};
    };
} // namespace vultra_app
