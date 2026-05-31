#pragma once

#include "common/file_dialog.hpp"
#include "editor_app/ui/editor_window.hpp"

#include <vultra/core/rhi/structs/extent2d.hpp>
#include <vultra/core/rhi/texture.hpp>
#include <vultra/function/services/imgui_service.hpp>
#include <vultra/function/services/render_service.hpp>

#include <cstdint>
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace vultra_app
{
    class RenderGraphWindow final : public EditorWindow
    {
    public:
        RenderGraphWindow();
        ~RenderGraphWindow() override;

        void draw(EditorContext& ctx) override;
        void requestRuntimeFrameGraphViewer();
        void drawRuntimeFrameGraphViewer(EditorContext& ctx);

    private:
        struct GraphEditorState;
        struct RuntimeGraphState;
        struct RenderTargetSlot
        {
            std::optional<vultra::rhi::Texture> texture;
            vultra::rhi::Extent2D               extent {};
            uint32_t                            layerCount {1};
            vultra::IImGuiService::TextureID    textureId {};
            uint64_t                            frameCreated {0};
            uint64_t                            releaseFrame {0};
        };
        struct TextureThumbnailEntry
        {
            const vultra::rhi::Texture*      texture {nullptr};
            vultra::IImGuiService::TextureID textureId {};
            uint64_t                         retireFrame {0};
        };

        void drawRuntimeGraph(EditorContext& ctx);
        void drawRuntimeGraphPopup(EditorContext& ctx);
        void drawRuntimeTexturePreviewWindow(EditorContext& ctx);
        void drawGraphEditor(EditorContext& ctx);
        void drawGraphEditorAddPopup(EditorContext& ctx);
        void drawGraphEditorCanvas(EditorContext& ctx);
        void drawPipelineEditorCanvas(EditorContext& ctx);
        void onClosed(EditorContext& ctx) override;
        void onDestroy(EditorContext& ctx) override;
        bool ensureRenderGraphPreviewCamera(EditorContext& ctx, uint32_t width, uint32_t height);
        void drawGameViewOverlay(EditorContext& ctx, ImVec2 childMin, ImVec2 childMax);
        void ensureOverlayRenderTarget(EditorContext& ctx, uint32_t width, uint32_t height, uint32_t layerCount = 1);
        void promotePendingOverlayRenderTarget(EditorContext& ctx);
        void retireOverlayRenderTarget(RenderTargetSlot& slot);
        void collectRetiredOverlayRenderTargets(EditorContext& ctx);
        void releaseOverlayRenderTarget(EditorContext& ctx);
        void collectRetiredTextureThumbnails(EditorContext& ctx);
        void releaseTextureThumbnails(EditorContext& ctx);
        void resetOverlayRenderTargetForProject(EditorContext& ctx);
        void suspendRuntimeGraphWindows(EditorContext& ctx);

        std::unique_ptr<RuntimeGraphState> m_RuntimeGraph;
        std::unique_ptr<GraphEditorState> m_GraphEditor;
        RenderTargetSlot              m_OverlayActiveRenderTarget;
        RenderTargetSlot              m_OverlayPendingRenderTarget;
        std::vector<RenderTargetSlot> m_OverlayRetiredRenderTargets;
        std::unordered_map<std::string, TextureThumbnailEntry> m_TextureThumbnailCache;
        std::vector<TextureThumbnailEntry> m_RetiredTextureThumbnails;
        float m_OverlayZoom {1.0f};
        float m_RuntimeGraphPreviewScale {1.0f};
        bool m_RuntimeGraphPreviewAutoFit {true};
        float m_RuntimeTexturePreviewScale {1.0f};
        bool m_RuntimeTexturePreviewAutoFit {true};
        bool m_RuntimeTexturePreviewGammaCorrect {true};
        bool m_RuntimeTexturePreviewChannels[4] {true, true, true, false};
        int  m_RuntimeTexturePreviewMode {0};
        float m_RuntimeTexturePreviewDepthNear {0.1f};
        float m_RuntimeTexturePreviewDepthFar {1000.0f};
        float m_RuntimeTexturePreviewClampMin {0.0f};
        float m_RuntimeTexturePreviewClampMax {1.0f};
        uint64_t m_ProjectGeneration {0};
        std::string m_RuntimeTexturePreviewKey;
        std::string m_RuntimeTexturePreviewTitle;
        std::string m_RuntimeTexturePreviewDefaultsKey;
        std::string m_RuntimeTexturePreviewOverrideKey;
        std::unordered_set<std::string> m_RuntimeTexturePreviewOverrideKeys;
        std::string m_PendingRuntimeTexturePreviewAutoFitKey;
        const vultra::rhi::Texture* m_PendingRuntimeTexturePreviewAutoFitTexture {nullptr};
        uint64_t m_PendingRuntimeTexturePreviewAutoFitFrame {0};
        uint64_t m_PendingRuntimeTexturePreviewAutoFitDeadlineFrame {0};
        uint64_t m_PendingRuntimeTexturePreviewAutoFitNextTryFrame {0};
        uint64_t m_RuntimeGraphTextureCaptureReadyFrame {0};
        std::unordered_set<std::string> m_RuntimeGraphTextureAutoFitDone;
        std::unordered_set<std::string> m_RuntimeGraphTextureDefaultPreviewDone;
        std::unordered_map<std::string, vultra::FrameGraphTexturePreviewSettings> m_RuntimeGraphTexturePreviewSettings;
        std::unordered_map<std::string, uint64_t> m_RuntimeGraphTextureAutoFitNextFrame;
        std::unordered_map<std::string, uint64_t> m_RuntimeGraphTextureAutoFitDeadlineFrame;
        std::vector<std::string> m_RenderGraphAssetUris;
        std::filesystem::path    m_RenderGraphAssetProject;
        std::string              m_RenderGraphAssetRoot;
        uint64_t                 m_RenderGraphAssetGeneration {0};
        std::filesystem::path    m_RenderGraphPassCatalogProject;
        std::string              m_RenderGraphPassCatalogAssetRoot;
        uint64_t                 m_RenderGraphPassCatalogAssetGeneration {0};
        ui::FileDialogField      m_BuiltinRenderGraphExportDialog {
            "BuiltinRenderGraphExportPath",
            "Export Builtin Render Graph",
            ui::FileDialogMode::File,
        };
        std::array<char, 512>    m_BuiltinRenderGraphExportPath {};
        bool m_RuntimeGraphPopupOpen {false};
        bool m_RuntimeGraphPopupPendingOpen {false};
        bool m_RuntimeTexturePreviewOpen {false};
        bool m_RuntimeTexturePreviewPopupPendingOpen {false};
        bool m_RuntimeGraphCleanupPending {false};
        bool m_RuntimeGraphSuspended {false};
    };
} // namespace vultra_app
