#pragma once

#include "common/asset_preview_cache.hpp"
#include "editor_app/ui/editor_window.hpp"

#include <array>
#include <filesystem>

namespace vultra_app
{
    class AssetBrowserWindow final : public EditorWindow
    {
    public:
        AssetBrowserWindow();

        void draw(EditorContext& ctx) override;

    private:
        void syncAssetRoot(const AppState& state);
        void drawDirectoryTree(const std::filesystem::path& path);
        void drawContentPanel(EditorContext& ctx);
        void drawListItem(EditorContext& ctx, const std::filesystem::directory_entry& entry);
        void drawGridItem(EditorContext& ctx, const std::filesystem::directory_entry& entry, float iconSize);
        void selectPath(EditorContext& ctx, const std::filesystem::path& path);

        std::filesystem::path m_AssetRoot;
        std::filesystem::path m_CurrentDir;
        std::filesystem::path m_SelectedPath;
        std::array<char, 128> m_Filter {};
        float                 m_LeftPanelRatio {0.28f};
        float                 m_IconSize {64.0f};
        float                 m_MinIconSize {24.0f};
        float                 m_MaxIconSize {128.0f};
        float                 m_ListThreshold {40.0f};
        ui::AssetPreviewCache m_PreviewCache;
    };
} // namespace vultra_app
