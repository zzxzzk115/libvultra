#pragma once

#include "common/asset_preview_cache.hpp"
#include "editor_app/ui/editor_window.hpp"

#include <array>
#include <filesystem>
#include <unordered_map>
#include <vector>

namespace vultra_app
{
    class AssetBrowserWindow final : public EditorWindow
    {
    public:
        AssetBrowserWindow();

        void draw(EditorContext& ctx) override;
        void onClosed(EditorContext& ctx) override;
        void onDestroy(EditorContext& ctx) override;

    private:
        void syncAssetRoot(EditorContext& ctx);
        void drawDirectoryTree(const std::filesystem::path& path);
        void drawContentPanel(EditorContext& ctx);
        void drawListItem(EditorContext& ctx, const std::filesystem::path& path);
        void drawGridItem(EditorContext& ctx, const std::filesystem::path& path, float iconSize);
        void handleDeferredSelection(EditorContext& ctx, const std::filesystem::path& path, bool hovered);
        void selectPath(EditorContext& ctx, const std::filesystem::path& path);
        void invalidateEntryCache();
        void drawContextMenu(EditorContext& ctx, const std::filesystem::path& path, bool isDirectory);
        void drawPendingPopups(EditorContext& ctx);
        const std::vector<std::filesystem::path>& entriesForCurrentDir();
        const std::vector<std::filesystem::path>& filteredEntriesForCurrentDir();

        std::filesystem::path m_AssetRoot;
        std::filesystem::path m_CurrentDir;
        std::filesystem::path m_SelectedPath;
        std::filesystem::path m_RenamingPath;
        std::filesystem::path m_DeletePath;
        std::filesystem::path m_PendingSelectPath;
        std::array<char, 128> m_Filter {};
        std::array<char, 128> m_RenameBuffer {};
        std::array<char, 128> m_NewFolderBuffer {"NewFolder"};
        float                 m_LeftPanelRatio {0.28f};
        float                 m_IconSize {64.0f};
        float                 m_MinIconSize {24.0f};
        float                 m_MaxIconSize {128.0f};
        float                 m_ListThreshold {40.0f};
        ui::AssetPreviewCache m_PreviewCache;
        std::filesystem::path m_CachedDir;
        std::string           m_CachedFilter;
        std::vector<std::filesystem::path> m_CachedEntries;
        std::vector<std::filesystem::path> m_CachedFilteredEntries;
        std::unordered_map<std::string, bool> m_VisibleChildDirectoryCache;
        int m_RemainingThumbnailLoads {0};
        bool m_PendingSelectDragging {false};
        bool m_OpenRenamePopup {false};
        bool m_OpenDeletePopup {false};
        bool m_OpenNewFolderPopup {false};
    };
} // namespace vultra_app
