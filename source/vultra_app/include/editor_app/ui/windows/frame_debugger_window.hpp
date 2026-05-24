#pragma once

#include "editor_app/ui/editor_window.hpp"

#include <vultra/function/services/imgui_service.hpp>

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

namespace vultra
{
    namespace rhi
    {
        class Texture;
    }
}

namespace vultra_app
{
    class FrameDebuggerWindow final : public EditorWindow
    {
    public:
        FrameDebuggerWindow();

        void draw(EditorContext& ctx) override;
        void onDestroy(EditorContext& ctx) override;

    private:
        struct TextureCacheEntry
        {
            const vultra::rhi::Texture* texture {nullptr};
            vultra::IImGuiService::TextureID textureId {};
            uint64_t retireFrame {0};
        };

        std::array<char, 128> m_Filter {};
        std::string           m_SelectedGraphKey;
        std::string           m_SelectedPassId;
        std::string           m_SelectedTextureKey;
        std::string           m_FrozenSnapshot;
        std::unordered_map<std::string, TextureCacheEntry> m_TextureCache;
        std::vector<TextureCacheEntry> m_RetiredTextureCache;
        bool                  m_UseFrozenSnapshot {false};
        bool                  m_TexturePreviewGammaCorrect {false};
        bool                  m_TexturePreviewChannels[4] {true, true, true, false};
        int                   m_TexturePreviewMode {0};
        float                 m_TexturePreviewScale {1.0f};
        bool                  m_TexturePreviewFitToView {true};
        float                 m_TexturePreviewDepthNear {0.1f};
        float                 m_TexturePreviewDepthFar {1000.0f};
        float                 m_TexturePreviewClampMin {0.0f};
        float                 m_TexturePreviewClampMax {1.0f};
        std::string           m_TexturePreviewDepthDefaultsKey;
        std::string           m_PendingTexturePreviewAutoFitKey;
        const vultra::rhi::Texture* m_PendingTexturePreviewAutoFitTexture {nullptr};
        uint64_t              m_PendingTexturePreviewAutoFitFrame {0};
        uint64_t              m_PendingTexturePreviewAutoFitDeadlineFrame {0};
        uint64_t              m_PendingTexturePreviewAutoFitNextTryFrame {0};
        int                   m_SelectedGraphIndex {0};
    };
} // namespace vultra_app
