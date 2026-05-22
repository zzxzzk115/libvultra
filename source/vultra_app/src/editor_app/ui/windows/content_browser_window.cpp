#include "editor_app/ui/windows/content_browser_window.hpp"

#include "common/ui_widgets.hpp"
#include "editor_app/selection.hpp"

#include <IconsMaterialDesignIcons.h>
#include <vultra/function/services/asset_service.hpp>
#include <vultra/function/services/render_service.hpp>
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace vultra_app
{
    namespace
    {
        constexpr const char* kAssetUuidPayload = "VULTRA_ASSET_UUID";

        bool passesFilter(const std::filesystem::path& path, const char* filter)
        {
            if (filter == nullptr || filter[0] == '\0')
                return true;

            auto name = path.filename().generic_string();
            auto text = std::string(filter);
            std::transform(name.begin(),
                           name.end(),
                           name.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            std::transform(text.begin(),
                           text.end(),
                           text.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            return name.find(text) != std::string::npos;
        }

        bool isEditorVisibleSourceAsset(const std::filesystem::path& assetRoot, const std::filesystem::path& path)
        {
            const auto filename = path.filename().generic_string();
            if (filename.empty())
                return false;

            std::error_code ec;
            const auto      rel = std::filesystem::relative(path, assetRoot, ec);
            if (!ec && !rel.empty() && *rel.begin() == "imported")
                return false;

            if (filename == "asset_registry.tsv")
                return false;

            const auto ext = path.extension();
            if (ext == ".vmanifest" || ext == ".vimport" || ext == ".vpk")
                return false;

            return true;
        }

        std::string lowerString(std::string value)
        {
            std::transform(value.begin(),
                           value.end(),
                           value.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            return value;
        }

        bool hasSuffix(std::string_view text, std::string_view suffix)
        {
            return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
        }

        bool isCodeEditableSourceAsset(const std::filesystem::path& path)
        {
            const auto name = lowerString(path.filename().generic_string());
            const auto ext  = lowerString(path.extension().generic_string());
            return ext == ".lua" || ext == ".vshader" || ext == ".glsl" || ext == ".vert" || ext == ".frag" ||
                   ext == ".comp" || ext == ".json" || ext == ".vproject" || ext == ".vscn" || ext == ".txt" ||
                   ext == ".md" || hasSuffix(name, ".vfeature.lua") || hasSuffix(name, ".vsrp.lua") ||
                   hasSuffix(name, ".vshaderlib.lua") || hasSuffix(name, ".vso.lua");
        }

        bool isRenderPipelineSource(const std::filesystem::path& path)
        {
            const auto name = lowerString(path.filename().generic_string());
            const auto ext  = lowerString(path.extension().generic_string());
            return ext == ".vshader" || ext == ".glsl" || ext == ".vert" || ext == ".frag" || ext == ".comp" ||
                   ext == ".json" || hasSuffix(name, ".vfeature.lua") || hasSuffix(name, ".vsrp.lua") ||
                   hasSuffix(name, ".vshaderlib.lua");
        }

        std::string pathToResUri(const EditorContext& ctx, const std::filesystem::path& path)
        {
            const auto root = (ctx.state.currentProject / ctx.state.currentAssetRoot).lexically_normal();
            std::error_code ec;
            const auto      rel     = std::filesystem::relative(path.lexically_normal(), root, ec);
            const auto      relText = rel.generic_string();
            if (ec || rel.empty() || relText == ".." || relText.starts_with("../"))
                return {};
            return "res://" + relText;
        }

        std::vector<std::filesystem::directory_entry> sortedEntries(const std::filesystem::path& path)
        {
            std::vector<std::filesystem::directory_entry> entries;
            std::error_code                               ec;
            for (const auto& entry : std::filesystem::directory_iterator(path, ec))
                entries.push_back(entry);

            std::sort(entries.begin(),
                      entries.end(),
                      [](const auto& a, const auto& b)
                      {
                          std::error_code ec;
                          const bool      aDir = a.is_directory(ec);
                          const bool      bDir = b.is_directory(ec);
                          if (aDir != bDir)
                              return aDir > bDir;
                          return a.path().filename().generic_string() < b.path().filename().generic_string();
                      });
            return entries;
        }

        std::vector<std::filesystem::path> sortedVisiblePaths(const std::filesystem::path& assetRoot,
                                                              const std::filesystem::path& dir)
        {
            std::vector<std::filesystem::path> entries;
            std::error_code                    ec;
            for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
            {
                const auto path = entry.path();
                if (isEditorVisibleSourceAsset(assetRoot, path))
                    entries.push_back(path);
            }

            std::sort(entries.begin(),
                      entries.end(),
                      [](const auto& a, const auto& b)
                      {
                          std::error_code ec;
                          const bool      aDir = std::filesystem::is_directory(a, ec);
                          const bool      bDir = std::filesystem::is_directory(b, ec);
                          if (aDir != bDir)
                              return aDir > bDir;
                          return a.filename().generic_string() < b.filename().generic_string();
                      });
            return entries;
        }

        void appendRecursiveVisiblePaths(const std::filesystem::path&        assetRoot,
                                         const std::filesystem::path&        dir,
                                         std::vector<std::filesystem::path>& out)
        {
            std::error_code ec;
            for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
            {
                const auto path = entry.path();
                if (!isEditorVisibleSourceAsset(assetRoot, path))
                    continue;
                out.push_back(path);
                if (entry.is_directory(ec))
                    appendRecursiveVisiblePaths(assetRoot, path, out);
            }
        }

        void copyPathName(std::array<char, 128>& dst, const std::filesystem::path& path)
        {
            const auto name  = path.filename().generic_string();
            const auto count = std::min(dst.size() - 1, name.size());
            std::memset(dst.data(), 0, dst.size());
            std::memcpy(dst.data(), name.data(), count);
        }

        std::string sourceAssetDisplayName(const std::filesystem::path& path, const bool isDirectory)
        {
            if (isDirectory)
                return path.filename().generic_string();

            auto name = path.stem().generic_string();
            if (name.empty())
                name = path.filename().generic_string();
            return name;
        }

        std::string sourceAssetUriFor(const EditorContext& ctx, const std::filesystem::path& path)
        {
            const auto assetRoot = ctx.state.currentProject / ctx.state.currentAssetRoot;
            std::error_code relEc;
            const auto      rel = std::filesystem::relative(path, assetRoot, relEc);
            if (relEc || rel.empty())
                return {};
            return "res://" + rel.generic_string();
        }

        bool resolveDraggableAsset(EditorContext& ctx, const std::filesystem::path& path, vultra::CoreUUID& out)
        {
            if (!ctx.services)
                return false;
            std::error_code ec;
            if (!std::filesystem::is_regular_file(path, ec))
                return false;

            auto* assetService = ctx.services->tryGet<vultra::IAssetService>();
            if (!assetService)
                return false;

            const auto uri = sourceAssetUriFor(ctx, path);
            if (uri.empty())
                return false;

            if (!assetService->resolver().reverseResolve(uri, out) || !out.valid())
                return false;

            return assetService->registry().lookup(out.native()).type != vasset::VAssetType::eUnknown;
        }

        void drawAssetDragSource(EditorContext& ctx, const std::filesystem::path& path)
        {
            vultra::CoreUUID uuid;
            if (!resolveDraggableAsset(ctx, path, uuid))
                return;

            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
            {
                ImGui::SetDragDropPayload(kAssetUuidPayload, &uuid, sizeof(uuid));
                ImGui::TextUnformatted(ui::sourceAssetIcon(path, false));
                ImGui::SameLine();
                ImGui::TextUnformatted(sourceAssetDisplayName(path, false).c_str());
                ImGui::TextDisabled("%s", uuid.toString().c_str());
                ImGui::EndDragDropSource();
            }
        }

        std::string sourceAssetTypeLabel(EditorContext& ctx, const std::filesystem::path& path, const bool isDirectory)
        {
            if (isDirectory)
                return "Folder";

            vultra::CoreUUID uuid;
            if (resolveDraggableAsset(ctx, path, uuid))
            {
                if (auto* assetService = ctx.services ? ctx.services->tryGet<vultra::IAssetService>() : nullptr)
                    return vasset::toString(assetService->registry().lookup(uuid.native()).type);
            }

            return "Source";
        }
    } // namespace

    ContentBrowserWindow::ContentBrowserWindow() : EditorWindow("Content Browser", ICON_MDI_FOLDER_MULTIPLE_IMAGE) {}

    void ContentBrowserWindow::onClosed(EditorContext& ctx) { m_PreviewCache.clear(ctx); }

    void ContentBrowserWindow::onDestroy(EditorContext& ctx) { m_PreviewCache.clear(ctx); }

    void ContentBrowserWindow::draw(EditorContext& ctx)
    {
        syncAssetRoot(ctx);

        ImGui::Begin(title().c_str(), &m_Open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        if (m_AssetRoot.empty())
        {
            ui::emptyState(ICON_MDI_FOLDER_OFF_OUTLINE, "No Asset Root", "Open or create a project to browse source assets.");
            ImGui::End();
            return;
        }

        const ImVec2 region            = ImGui::GetContentRegionAvail();
        const float  splitterThickness = 4.0f;
        m_LeftPanelRatio               = std::clamp(m_LeftPanelRatio, 0.18f, 0.55f);

        const float leftWidth  = region.x * m_LeftPanelRatio;
        const float rightWidth = region.x - leftWidth - splitterThickness;

        ImGui::BeginChild("##AssetTree", ImVec2(leftWidth, 0), true);
        if (std::filesystem::exists(m_AssetRoot))
        {
            ImGui::SetNextItemOpen(true, ImGuiCond_Once);
            if (ImGui::TreeNodeEx(m_AssetRoot.filename().generic_string().c_str(),
                                  ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth))
            {
                if (ImGui::IsItemClicked())
                    m_CurrentDir = m_AssetRoot;
                drawDirectoryTree(m_AssetRoot);
                ImGui::TreePop();
            }
        }
        else
        {
            ImGui::TextWrapped("Missing asset root:\n%s", m_AssetRoot.generic_string().c_str());
        }
        ImGui::EndChild();

        ImGui::SameLine(0, 0);
        ImGui::Button("##AssetBrowserSplitter", ImVec2(splitterThickness, -1));
        if (ImGui::IsItemActive() && region.x > 0.0f)
            m_LeftPanelRatio += ImGui::GetIO().MouseDelta.x / region.x;

        ImGui::SameLine(0, 0);
        ImGui::BeginChild("##AssetContent", ImVec2(rightWidth, 0), true);
        drawContentPanel(ctx);
        drawPendingPopups(ctx);
        ImGui::EndChild();

        ImGui::End();
    }

    void ContentBrowserWindow::syncAssetRoot(EditorContext& ctx)
    {
        const auto& state = ctx.state;
        const auto assetRoot =
            state.currentProject.empty() ? std::filesystem::path {} : state.currentProject / state.currentAssetRoot;
        if (assetRoot == m_AssetRoot)
            return;

        m_PreviewCache.clear(ctx);
        m_AssetRoot    = assetRoot.lexically_normal();
        m_CurrentDir   = m_AssetRoot;
        m_SelectedPath.clear();
        invalidateEntryCache();
    }

    void ContentBrowserWindow::drawDirectoryTree(const std::filesystem::path& path)
    {
        for (const auto& entry : sortedEntries(path))
        {
            std::error_code ec;
            if (!entry.is_directory(ec))
                continue;

            const auto dir = entry.path();
            if (!isEditorVisibleSourceAsset(m_AssetRoot, dir))
                continue;

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
            if (dir == m_CurrentDir)
                flags |= ImGuiTreeNodeFlags_Selected;

            const auto cacheKey = dir.lexically_normal().generic_string();
            auto       leafIt   = m_VisibleChildDirectoryCache.find(cacheKey);
            if (leafIt == m_VisibleChildDirectoryCache.end())
            {
                bool hasChild = false;
                std::error_code childEc;
                for (const auto& child : std::filesystem::directory_iterator(dir, childEc))
                {
                    if (child.is_directory(childEc) && isEditorVisibleSourceAsset(m_AssetRoot, child.path()))
                    {
                        hasChild = true;
                        break;
                    }
                }
                leafIt = m_VisibleChildDirectoryCache.emplace(cacheKey, hasChild).first;
            }

            const bool isLeaf = !leafIt->second;
            if (isLeaf)
                flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

            const bool opened = ImGui::TreeNodeEx(dir.filename().generic_string().c_str(), flags);
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
                m_CurrentDir = dir;
            if (opened && !isLeaf)
            {
                drawDirectoryTree(dir);
                ImGui::TreePop();
            }
        }
    }

    void ContentBrowserWindow::drawContentPanel(EditorContext& ctx)
    {
        ImGui::TextUnformatted(ICON_MDI_MAGNIFY);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##AssetFilter", "Filter source assets...", m_Filter.data(), m_Filter.size());

        ImGui::TextUnformatted(ICON_MDI_RESIZE " Icon Size:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat("##AssetIconSize", &m_IconSize, m_MinIconSize, m_MaxIconSize, "%.0f px");

        if (!m_AssetRoot.empty() && !m_CurrentDir.empty())
        {
            std::error_code ec;
            const auto      rel = std::filesystem::relative(m_CurrentDir, m_AssetRoot, ec);
            if (ImGui::SmallButton(m_AssetRoot.filename().generic_string().c_str()))
                m_CurrentDir = m_AssetRoot;

            if (!ec)
            {
                auto accum = m_AssetRoot;
                for (const auto& part : rel)
                {
                    if (part == ".")
                        continue;
                    ImGui::SameLine();
                    ImGui::TextUnformatted(ICON_MDI_CHEVRON_RIGHT);
                    ImGui::SameLine();
                    accum /= part;
                    if (ImGui::SmallButton(part.generic_string().c_str()))
                        m_CurrentDir = accum;
                }
            }
        }
        ImGui::Separator();

        if (m_CurrentDir.empty() || !std::filesystem::exists(m_CurrentDir))
        {
            ui::emptyState(ICON_MDI_FOLDER_ALERT_OUTLINE, "Missing Directory", "The selected folder no longer exists.");
            return;
        }

        if (ImGui::BeginPopupContextWindow("AssetBrowserEmptyContext",
                                           ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
        {
            if (ImGui::MenuItem(ICON_MDI_FOLDER_PLUS "  Create Folder"))
            {
                std::memset(m_NewFolderBuffer.data(), 0, m_NewFolderBuffer.size());
                std::strncpy(m_NewFolderBuffer.data(), "NewFolder", m_NewFolderBuffer.size() - 1);
                m_OpenNewFolderPopup = true;
            }
            ImGui::EndPopup();
        }

        const bool listMode = m_IconSize < m_ListThreshold;
        const auto& entries = filteredEntriesForCurrentDir();
        m_RemainingThumbnailLoads = 8;
        m_PreviewCache.trim(ctx, 96);
        m_GridItemBounds.clear();

        if (listMode)
        {
            if (ImGui::BeginTable("AssetContentTable",
                                  3,
                                  ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                      ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("Name");
                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableSetupColumn("Path");
                ImGui::TableHeadersRow();

                for (const auto& entry : entries)
                    drawListItem(ctx, entry);

                ImGui::EndTable();
            }
            return;
        }

        const float cellPadding = 12.0f;
        const float cellWidth   = m_IconSize + cellPadding;
        const float panelWidth  = std::max(1.0f, ImGui::GetContentRegionAvail().x);
        const ImVec2 gridMin    = ImGui::GetCursorScreenPos();
        const ImVec2 gridMax {gridMin.x + panelWidth, gridMin.y + ImGui::GetContentRegionAvail().y};
        const int   columns     = std::max(1, static_cast<int>(panelWidth / cellWidth));

        ImGui::Columns(columns, nullptr, false);
        for (const auto& entry : entries)
            drawGridItem(ctx, entry, m_IconSize);
        ImGui::Columns(1);

        if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemActive() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            const ImVec2 mouse = ImGui::GetMousePos();
            const bool overItem = std::any_of(m_GridItemBounds.begin(),
                                              m_GridItemBounds.end(),
                                              [&](const GridItemBounds& item)
                                              {
                                                  return mouse.x >= item.min.x && mouse.x <= item.max.x &&
                                                         mouse.y >= item.min.y && mouse.y <= item.max.y;
                                              });
            if (!overItem && mouse.x >= gridMin.x && mouse.x <= gridMax.x && mouse.y >= gridMin.y && mouse.y <= gridMax.y)
                beginBoxSelection(mouse);
        }
        updateBoxSelection(ctx);
    }

    void ContentBrowserWindow::drawListItem(EditorContext& ctx, const std::filesystem::path& path)
    {
        std::error_code ec;
        const bool      isDir = std::filesystem::is_directory(path, ec);

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        const auto label = std::string(ui::sourceAssetIcon(path, isDir)) + "  " + sourceAssetDisplayName(path, isDir);
        ImGui::Selectable(label.c_str(), m_SelectedPath == path, ImGuiSelectableFlags_SpanAllColumns);
        const bool hovered = ImGui::IsItemHovered();
        handleDeferredSelection(ctx, path, hovered);
        if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            openPath(ctx, path);
        if (!isDir)
            drawAssetDragSource(ctx, path);
        if (ImGui::BeginPopupContextItem("AssetListContext"))
        {
            drawContextMenu(ctx, path, isDir);
            ImGui::EndPopup();
        }

        ImGui::TableNextColumn();
        const auto type = sourceAssetTypeLabel(ctx, path, isDir);
        ImGui::TextUnformatted(type.c_str());
        ImGui::TableNextColumn();
        ImGui::TextWrapped("%s", path.lexically_normal().generic_string().c_str());
    }

    void ContentBrowserWindow::drawGridItem(EditorContext& ctx, const std::filesystem::path& path, float iconSize)
    {
        std::error_code ec;
        const bool      isDir = std::filesystem::is_directory(path, ec);
        const auto      name  = sourceAssetDisplayName(path, isDir);

        ImGui::PushID(path.generic_string().c_str());
        ImGui::BeginGroup();

        const bool selected = isPathSelected(path);

        ImTextureID previewId {};
        if (isDir)
        {
            previewId = m_PreviewCache.getBuiltinIcon(ctx, ui::BuiltinAssetIcon::Folder, iconSize);
        }
        else if (ui::isTextureSourceAsset(path))
        {
            const bool cached    = m_PreviewCache.hasCachedTexturePreview(ctx, path);
            const bool allowLoad = cached || m_RemainingThumbnailLoads > 0;
            previewId            = m_PreviewCache.getTexturePreview(ctx, path, allowLoad);
            if (!cached && allowLoad)
                --m_RemainingThumbnailLoads;
        }
        if (previewId)
        {
            const ImVec2 iconPos = ImGui::GetCursorScreenPos();
            auto*        drawList = ImGui::GetWindowDrawList();
            if (selected)
            {
                drawList->AddRectFilled(ImVec2 {iconPos.x - 8.0f, iconPos.y - 8.0f},
                                        ImVec2 {iconPos.x + iconSize + 8.0f, iconPos.y + iconSize + 30.0f},
                                        IM_COL32(28, 45, 62, 230),
                                        5.0f);
                drawList->AddRectFilled(ImVec2 {iconPos.x - 8.0f, iconPos.y - 8.0f},
                                        ImVec2 {iconPos.x + iconSize + 8.0f, iconPos.y - 3.0f},
                                        IM_COL32(45, 145, 230, 230),
                                        5.0f,
                                        ImDrawFlags_RoundCornersTop);
            }
            ImGui::Image(previewId, ImVec2(iconSize, iconSize));
            drawList->AddRect(ImGui::GetItemRectMin(),
                              ImGui::GetItemRectMax(),
                              selected ? IM_COL32(68, 160, 242, 230) : ImGui::GetColorU32(ImGuiCol_Border),
                              isDir ? 8.0f : 4.0f);
        }
        else
        {
            ImGui::Button(ui::sourceAssetIcon(path, isDir), ImVec2(iconSize, iconSize));
        }

        const bool hovered = ImGui::IsItemHovered();
        handleDeferredSelection(ctx, path, hovered);
        if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            openPath(ctx, path);
        if (!isDir)
            drawAssetDragSource(ctx, path);
        if (ImGui::BeginPopupContextItem("AssetGridContext"))
        {
            drawContextMenu(ctx, path, isDir);
            ImGui::EndPopup();
        }

        const float textWidth = iconSize + 10.0f;
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + textWidth);
        ImGui::TextWrapped("%s", name.c_str());
        ImGui::PopTextWrapPos();

        ImGui::EndGroup();
        m_GridItemBounds.push_back(GridItemBounds {
            .path = path,
            .min  = ImGui::GetItemRectMin(),
            .max  = ImGui::GetItemRectMax(),
        });
        ImGui::NextColumn();
        ImGui::PopID();
    }

    void ContentBrowserWindow::handleDeferredSelection(EditorContext& ctx,
                                                     const std::filesystem::path& path,
                                                     bool                         hovered)
    {
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        {
            m_PendingSelectPath     = path;
            m_PendingSelectDragging = false;
        }

        if (m_PendingSelectPath != path)
            return;

        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, ImGui::GetIO().MouseDragThreshold))
            m_PendingSelectDragging = true;

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            if (hovered && !m_PendingSelectDragging)
                selectPath(ctx, path);

            m_PendingSelectPath.clear();
            m_PendingSelectDragging = false;
        }
    }

    void ContentBrowserWindow::selectPath(EditorContext& ctx, const std::filesystem::path& path)
    {
        if (!std::filesystem::exists(path))
            return;

        m_SelectedPath                  = path;
        ctx.state.selectedSourceAsset   = path.lexically_normal();
        m_SelectedPaths.clear();
        m_SelectedPaths.push_back(path);
        Selection::clear(SelectionCategory::Entity);
        Selection::clear(SelectionCategory::Asset);
    }

    void ContentBrowserWindow::openPath(EditorContext& ctx, const std::filesystem::path& path)
    {
        selectPath(ctx, path);
        if (std::filesystem::is_directory(path))
        {
            m_CurrentDir = path;
            invalidateEntryCache();
        }
        else if (isCodeEditableSourceAsset(path))
        {
            ctx.state.codeEditorPath          = path.lexically_normal();
            ctx.state.codeEditorOpenRequested = true;
            ctx.state.statusMessage           = "Opened in Code Editor: " + path.filename().generic_string();
        }
    }

    void ContentBrowserWindow::beginBoxSelection(const ImVec2& start)
    {
        m_BoxSelecting   = true;
        m_BoxSelectStart = start;
        m_BoxSelectEnd   = start;
        m_PendingSelectPath.clear();
        m_PendingSelectDragging = false;
    }

    bool ContentBrowserWindow::isPathSelected(const std::filesystem::path& path) const
    {
        return std::find(m_SelectedPaths.begin(), m_SelectedPaths.end(), path) != m_SelectedPaths.end() ||
               m_SelectedPath == path;
    }

    void ContentBrowserWindow::updateBoxSelection(EditorContext& ctx)
    {
        if (!m_BoxSelecting)
            return;

        m_BoxSelectEnd = ImGui::GetMousePos();
        const ImVec2 min {std::min(m_BoxSelectStart.x, m_BoxSelectEnd.x), std::min(m_BoxSelectStart.y, m_BoxSelectEnd.y)};
        const ImVec2 max {std::max(m_BoxSelectStart.x, m_BoxSelectEnd.x), std::max(m_BoxSelectStart.y, m_BoxSelectEnd.y)};
        auto* drawList = ImGui::GetForegroundDrawList(ImGui::GetWindowViewport());
        drawList->PushClipRect(ImGui::GetWindowPos(), ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y), true);
        drawList->AddRectFilled(min, max, IM_COL32(35, 110, 180, 54), 2.0f);
        drawList->AddRect(min, max, IM_COL32(80, 176, 255, 220), 2.0f, 0, 1.35f);
        drawList->PopClipRect();

        m_SelectedPaths.clear();
        for (const auto& item : m_GridItemBounds)
        {
            const bool intersects = item.max.x >= min.x && item.min.x <= max.x &&
                                    item.max.y >= min.y && item.min.y <= max.y;
            if (intersects)
                m_SelectedPaths.push_back(item.path);
        }

        if (!m_SelectedPaths.empty())
        {
            m_SelectedPath = m_SelectedPaths.back();
            ctx.state.selectedSourceAsset = m_SelectedPath.lexically_normal();
            Selection::clear(SelectionCategory::Entity);
            Selection::clear(SelectionCategory::Asset);
        }

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            m_BoxSelecting = false;
    }

    void ContentBrowserWindow::invalidateEntryCache()
    {
        m_CachedDir.clear();
        m_CachedFilter.clear();
        m_CachedEntries.clear();
        m_CachedFilteredEntries.clear();
        m_VisibleChildDirectoryCache.clear();
    }

    void ContentBrowserWindow::drawContextMenu(EditorContext& ctx, const std::filesystem::path& path, bool isDirectory)
    {
        if (ImGui::MenuItem(isDirectory ? ICON_MDI_FOLDER_OPEN "  Open" : ICON_MDI_EYE "  Inspect"))
        {
            if (isDirectory)
                openPath(ctx, path);
            else
                selectPath(ctx, path);
        }
        if (!isDirectory && isCodeEditableSourceAsset(path))
        {
            if (ImGui::MenuItem(ICON_MDI_FILE_DOCUMENT_EDIT "  Edit Source"))
                openPath(ctx, path);
        }
        if (ImGui::MenuItem(ICON_MDI_REFRESH "  Reimport"))
        {
            bool refreshed = false;
            if (ctx.services)
            {
                if (auto* assetService = ctx.services->tryGet<vultra::IAssetService>())
                {
                    const auto uri = pathToResUri(ctx, path);
                    if (!uri.empty())
                        refreshed = assetService->reimportAsset(uri, true);
                }
                if (!isDirectory && isRenderPipelineSource(path))
                {
                    if (auto* renderService = ctx.services->tryGet<vultra::IRenderService>())
                        refreshed = renderService->reloadRenderPipeline() || refreshed;
                }
            }
            ctx.state.statusMessage = refreshed ? "Reimported source asset." : "Failed to reimport source asset.";
        }
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_MDI_FOLDER_PLUS "  Create Folder"))
        {
            m_CurrentDir = isDirectory ? path : path.parent_path();
            std::memset(m_NewFolderBuffer.data(), 0, m_NewFolderBuffer.size());
            std::strncpy(m_NewFolderBuffer.data(), "NewFolder", m_NewFolderBuffer.size() - 1);
            m_OpenNewFolderPopup = true;
        }
        if (ImGui::MenuItem(ICON_MDI_PENCIL "  Rename"))
        {
            m_RenamingPath = path;
            copyPathName(m_RenameBuffer, path);
            m_OpenRenamePopup = true;
        }
        if (ImGui::MenuItem(ICON_MDI_DELETE "  Delete"))
        {
            m_DeletePath = path;
            m_OpenDeletePopup = true;
        }
    }

    void ContentBrowserWindow::drawPendingPopups(EditorContext& ctx)
    {
        if (m_OpenRenamePopup)
        {
            ImGui::OpenPopup("Rename Asset");
            m_OpenRenamePopup = false;
        }
        if (ImGui::BeginPopupModal("Rename Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextWrapped("%s", m_RenamingPath.generic_string().c_str());
            ImGui::InputText("Name", m_RenameBuffer.data(), m_RenameBuffer.size());
            if (ImGui::Button("Rename"))
            {
                std::error_code ec;
                const auto      dst = m_RenamingPath.parent_path() / m_RenameBuffer.data();
                std::filesystem::rename(m_RenamingPath, dst, ec);
                ctx.state.statusMessage = ec ? "Rename failed: " + ec.message() : "Renamed asset.";
                if (!ec)
                    m_SelectedPath = dst;
                invalidateEntryCache();
                m_RenamingPath.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (m_OpenNewFolderPopup)
        {
            ImGui::OpenPopup("Create Folder");
            m_OpenNewFolderPopup = false;
        }
        if (ImGui::BeginPopupModal("Create Folder", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::InputText("Name", m_NewFolderBuffer.data(), m_NewFolderBuffer.size());
            if (ImGui::Button("Create"))
            {
                std::error_code ec;
                std::filesystem::create_directories(m_CurrentDir / m_NewFolderBuffer.data(), ec);
                ctx.state.statusMessage = ec ? "Create folder failed: " + ec.message() : "Created folder.";
                invalidateEntryCache();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (m_OpenDeletePopup)
        {
            ImGui::OpenPopup("Delete Asset");
            m_OpenDeletePopup = false;
        }
        if (ImGui::BeginPopupModal("Delete Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextWrapped("Delete this source asset?");
            ImGui::TextWrapped("%s", m_DeletePath.generic_string().c_str());
            if (ImGui::Button("Delete"))
            {
                std::error_code ec;
                if (std::filesystem::is_directory(m_DeletePath, ec))
                    std::filesystem::remove_all(m_DeletePath, ec);
                else
                    std::filesystem::remove(m_DeletePath, ec);
                ctx.state.statusMessage = ec ? "Delete failed: " + ec.message() : "Deleted asset.";
                if (m_SelectedPath == m_DeletePath)
                {
                    m_SelectedPath.clear();
                    ctx.state.selectedSourceAsset.clear();
                }
                invalidateEntryCache();
                m_DeletePath.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    const std::vector<std::filesystem::path>& ContentBrowserWindow::entriesForCurrentDir()
    {
        if (m_CurrentDir != m_CachedDir)
        {
            m_CachedDir     = m_CurrentDir;
            m_CachedEntries = sortedVisiblePaths(m_AssetRoot, m_CurrentDir);
            m_CachedFilter.clear();
            m_CachedFilteredEntries.clear();
        }
        return m_CachedEntries;
    }

    const std::vector<std::filesystem::path>& ContentBrowserWindow::filteredEntriesForCurrentDir()
    {
        const auto filter = std::string(m_Filter.data());
        if (filter == m_CachedFilter && m_CurrentDir == m_CachedDir && !m_CachedFilteredEntries.empty())
            return m_CachedFilteredEntries;

        m_CachedFilter      = filter;
        m_CachedFilteredEntries.clear();
        if (!filter.empty())
        {
            appendRecursiveVisiblePaths(m_AssetRoot, m_CurrentDir, m_CachedFilteredEntries);
            std::sort(m_CachedFilteredEntries.begin(), m_CachedFilteredEntries.end(), [](const auto& a, const auto& b) {
                std::error_code ec;
                const bool      aDir = std::filesystem::is_directory(a, ec);
                const bool      bDir = std::filesystem::is_directory(b, ec);
                if (aDir != bDir)
                    return aDir > bDir;
                return a.generic_string() < b.generic_string();
            });
            std::vector<std::filesystem::path> recursiveEntries;
            recursiveEntries.swap(m_CachedFilteredEntries);
            m_CachedFilteredEntries.reserve(recursiveEntries.size());
            for (const auto& path : recursiveEntries)
            {
                if (passesFilter(path, m_Filter.data()))
                    m_CachedFilteredEntries.push_back(path);
            }
            return m_CachedFilteredEntries;
        }

        const auto& entries = entriesForCurrentDir();
        m_CachedFilteredEntries.reserve(entries.size());
        for (const auto& path : entries)
        {
            if (passesFilter(path, m_Filter.data()))
                m_CachedFilteredEntries.push_back(path);
        }
        return m_CachedFilteredEntries;
    }
} // namespace vultra_app
