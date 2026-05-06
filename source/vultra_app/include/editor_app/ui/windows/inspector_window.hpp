#pragma once

#include "editor_app/ui/editor_window.hpp"
#include "common/asset_preview_cache.hpp"

#include <vultra/core/base/uuid.hpp>

#include <array>
#include <filesystem>

namespace vultra_app
{
    class InspectorWindow final : public EditorWindow
    {
    public:
        InspectorWindow();

        void draw(EditorContext& ctx) override;

    private:
        void drawEntityInspector(EditorContext& ctx);
        void drawAssetInspector(EditorContext& ctx);
        void drawSourceAssetInspector(EditorContext& ctx);
        void drawSourceTexturePreview(EditorContext& ctx, const std::filesystem::path& path);

        vultra::CoreUUID      m_NameEditEntity {};
        std::array<char, 128> m_NameBuffer {};
        ui::AssetPreviewCache m_PreviewCache;
    };
} // namespace vultra_app
