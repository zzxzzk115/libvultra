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
        int                   m_SelectedGraphIndex {0};
    };
} // namespace vultra_app
