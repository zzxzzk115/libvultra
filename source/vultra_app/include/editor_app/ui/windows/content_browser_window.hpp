#pragma once

#include "common/asset_preview_cache.hpp"
#include "editor_app/ui/editor_window.hpp"

#include <imgui.h>
#include <array>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace vultra_app
{
    struct ModelSubAssetEntry
    {
        std::string uuid;
        std::string name;
        std::string importedPath;
    };

    class ContentBrowserWindow final : public EditorWindow
    {
    public:
        ContentBrowserWindow();

        void tick(EditorContext& ctx) override;
        void draw(EditorContext& ctx) override;
        void onClosed(EditorContext& ctx) override;
        void onDestroy(EditorContext& ctx) override;

    private:
        void syncAssetRoot(EditorContext& ctx);
        void drawDirectoryTree(const std::filesystem::path& path);
        void drawContentPanel(EditorContext& ctx);
        void drawListItem(EditorContext& ctx, const std::filesystem::path& path);
        void drawGridItem(EditorContext& ctx, const std::filesystem::path& path, float iconSize);
        void drawListSubAsset(EditorContext&                ctx,
                              const std::filesystem::path& ownerPath,
                              const std::string&           uuid,
                              const std::string&           name,
                              const std::string&           importedPath);
        void drawGridSubAsset(EditorContext&                ctx,
                              const std::filesystem::path& ownerPath,
                              const std::string&           uuid,
                              const std::string&           name,
                              const std::string&           importedPath,
                              float                        iconSize);
        void handleDeferredSelection(EditorContext& ctx, const std::filesystem::path& path, bool hovered);
        void selectPath(EditorContext& ctx, const std::filesystem::path& path);
        void togglePathSelection(EditorContext& ctx, const std::filesystem::path& path);
        void selectPathRange(EditorContext& ctx, const std::filesystem::path& path);
        void openPath(EditorContext& ctx, const std::filesystem::path& path);
        void beginBoxSelection(const ImVec2& start);
        void updateBoxSelection(EditorContext& ctx);
        bool isPathSelected(const std::filesystem::path& path) const;
        void invalidateEntryCache();
        void drawContextMenu(EditorContext& ctx, const std::filesystem::path& path, bool isDirectory);
        void drawPendingPopups(EditorContext& ctx);
        void drawCreateAssetMenu(EditorContext& ctx, const std::filesystem::path& targetDir);
        void openCreateAssetPopup(const std::string& creatorId, const std::filesystem::path& targetDir);
        void refreshCreateAssetShaderOptions();
        bool createRegisteredAsset(EditorContext& ctx);
        void openImportDialog(const std::filesystem::path& targetDir, bool directory);
        void openPackageImportDialog();
        void openPackageExportDialog(const std::filesystem::path& contextPath);
        void drawImportDialogs(EditorContext& ctx);
        void importExternalPath(EditorContext& ctx, const std::filesystem::path& source);
        bool importPackage(EditorContext& ctx, const std::filesystem::path& packagePath);
        bool exportSelectedPackage(EditorContext&              ctx,
                                   const std::filesystem::path& packagePath,
                                   const std::filesystem::path& contextPath);
        std::vector<std::filesystem::path> selectedPathsForContext(const std::filesystem::path& contextPath) const;
        const std::vector<std::filesystem::path>& entriesForCurrentDir();
        const std::vector<std::filesystem::path>& filteredEntriesForCurrentDir();
        const std::vector<std::filesystem::path>& directoryChildrenFor(const std::filesystem::path& path);
        const std::vector<ModelSubAssetEntry>& modelSubAssetsFor(EditorContext& ctx, const std::filesystem::path& path);

        std::filesystem::path m_AssetRoot;
        std::filesystem::path m_CurrentDir;
        std::filesystem::path m_SelectedPath;
        std::filesystem::path m_RenamingPath;
        std::filesystem::path m_DeletePath;
        std::filesystem::path m_ImportTargetDir;
        std::filesystem::path m_PackageExportContextPath;
        std::filesystem::path m_CreateAssetTargetDir;
        std::filesystem::path m_PendingSelectPath;
        std::filesystem::path m_LastClickedPath;
        std::array<char, 128> m_Filter {};
        std::array<char, 128> m_RenameBuffer {};
        std::array<char, 128> m_NewFolderBuffer {"NewFolder"};
        std::array<char, 128> m_CreateAssetNameBuffer {};
        std::string           m_CreateAssetCreatorId;
        std::vector<std::string> m_CreateAssetShaderOptions;
        int                      m_CreateAssetShaderIndex {0};
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
        std::unordered_map<std::string, std::vector<std::filesystem::path>> m_DirectoryChildCache;
        std::unordered_map<std::string, bool> m_VisibleChildDirectoryCache;
        std::unordered_map<std::string, std::vector<ModelSubAssetEntry>> m_ModelSubAssetCache;
        uint64_t m_ModelSubAssetCacheGeneration {0};
        uint64_t m_ObservedAssetFileGeneration {0};
        std::unordered_set<std::string> m_ExpandedModelAssets;
        int m_RemainingThumbnailLoads {0};
        double m_LastClickTime {0.0};
        bool m_PendingSelectDragging {false};
        bool m_BoxSelecting {false};
        ImVec2 m_BoxSelectStart {};
        ImVec2 m_BoxSelectEnd {};
        struct GridItemBounds
        {
            std::filesystem::path path;
            ImVec2                min {};
            ImVec2                max {};
        };
        std::vector<GridItemBounds> m_GridItemBounds;
        std::vector<std::filesystem::path> m_SelectedPaths;
        bool m_OpenRenamePopup {false};
        bool m_OpenDeletePopup {false};
        bool m_OpenNewFolderPopup {false};
        bool m_OpenCreateAssetPopup {false};
    };
} // namespace vultra_app
