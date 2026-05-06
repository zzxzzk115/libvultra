#include "editor_app/ui/windows/asset_browser_window.hpp"

#include "editor_app/selection.hpp"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <filesystem>
#include <string>
#include <vector>

namespace vultra_app
{
    namespace
    {
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

        bool hasExtension(const std::filesystem::path& path, std::initializer_list<const char*> exts)
        {
            auto ext = path.extension().generic_string();
            std::transform(ext.begin(),
                           ext.end(),
                           ext.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            return std::any_of(exts.begin(), exts.end(), [&](const char* candidate) { return ext == candidate; });
        }

        const char* iconForPath(const std::filesystem::path& path, bool isDir)
        {
            if (isDir)
                return "[DIR]";
            if (hasExtension(path, {".png", ".jpg", ".jpeg", ".bmp", ".tga", ".hdr", ".ktx2"}))
                return "[IMG]";
            if (hasExtension(path, {".vscn"}))
                return "[SCN]";
            if (hasExtension(path, {".gltf", ".glb", ".obj", ".fbx", ".ply", ".spz"}))
                return "[3D]";
            if (hasExtension(path, {".lua"}))
                return "[LUA]";
            return "[FILE]";
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

        bool hasVisibleChildDirectory(const std::filesystem::path& assetRoot, const std::filesystem::path& path)
        {
            std::error_code ec;
            for (const auto& entry : std::filesystem::directory_iterator(path, ec))
            {
                if (entry.is_directory(ec) && isEditorVisibleSourceAsset(assetRoot, entry.path()))
                    return true;
            }
            return false;
        }
    } // namespace

    AssetBrowserWindow::AssetBrowserWindow() : EditorWindow("Assets") {}

    void AssetBrowserWindow::draw(EditorContext& ctx)
    {
        syncAssetRoot(ctx.state);

        ImGui::Begin(m_Name.c_str(), &m_Open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        if (m_AssetRoot.empty())
        {
            ImGui::TextUnformatted("No project asset root is available.");
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
        ImGui::EndChild();

        ImGui::End();
    }

    void AssetBrowserWindow::syncAssetRoot(const AppState& state)
    {
        const auto assetRoot =
            state.currentProject.empty() ? std::filesystem::path {} : state.currentProject / state.currentAssetRoot;
        if (assetRoot == m_AssetRoot)
            return;

        m_AssetRoot    = assetRoot.lexically_normal();
        m_CurrentDir   = m_AssetRoot;
        m_SelectedPath.clear();
    }

    void AssetBrowserWindow::drawDirectoryTree(const std::filesystem::path& path)
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

            const bool isLeaf = !hasVisibleChildDirectory(m_AssetRoot, dir);
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

    void AssetBrowserWindow::drawContentPanel(EditorContext& ctx)
    {
        ImGui::TextUnformatted("Filter:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##AssetFilter", m_Filter.data(), m_Filter.size());

        ImGui::TextUnformatted("Icon Size:");
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
                    ImGui::TextUnformatted(">");
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
            ImGui::TextUnformatted("Directory does not exist.");
            return;
        }

        const bool listMode = m_IconSize < m_ListThreshold;
        const auto entries  = sortedEntries(m_CurrentDir);

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
        const int   columns     = std::max(1, static_cast<int>(panelWidth / cellWidth));

        ImGui::Columns(columns, nullptr, false);
        for (const auto& entry : entries)
            drawGridItem(ctx, entry, m_IconSize);
        ImGui::Columns(1);
    }

    void AssetBrowserWindow::drawListItem(EditorContext& ctx, const std::filesystem::directory_entry& entry)
    {
        const auto path = entry.path();
        if (!isEditorVisibleSourceAsset(m_AssetRoot, path) || !passesFilter(path, m_Filter.data()))
            return;

        std::error_code ec;
        const bool      isDir = entry.is_directory(ec);

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        const auto label = std::string(iconForPath(path, isDir)) + " " + path.filename().generic_string();
        if (ImGui::Selectable(label.c_str(), m_SelectedPath == path, ImGuiSelectableFlags_SpanAllColumns))
            selectPath(ctx, path);
        if (isDir && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            m_CurrentDir = path;

        ImGui::TableNextColumn();
        const auto ext = path.extension().generic_string();
        ImGui::TextUnformatted(isDir ? "Folder" : ext.c_str());
        ImGui::TableNextColumn();
        ImGui::TextWrapped("%s", path.lexically_normal().generic_string().c_str());
    }

    void AssetBrowserWindow::drawGridItem(EditorContext& ctx,
                                          const std::filesystem::directory_entry& entry,
                                          float                                  iconSize)
    {
        const auto path = entry.path();
        if (!isEditorVisibleSourceAsset(m_AssetRoot, path) || !passesFilter(path, m_Filter.data()))
            return;

        std::error_code ec;
        const bool      isDir = entry.is_directory(ec);
        const auto      name  = path.filename().generic_string();

        ImGui::PushID(path.generic_string().c_str());
        ImGui::BeginGroup();

        const bool selected = m_SelectedPath == path;
        if (selected)
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_Header));

        const auto previewId = (!isDir && ui::isTextureSourceAsset(path)) ? m_PreviewCache.getTexturePreview(ctx, path) : ImTextureID {};
        if (previewId)
        {
            ImGui::Image(previewId, ImVec2(iconSize, iconSize));
            ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(),
                                                ImGui::GetItemRectMax(),
                                                ImGui::GetColorU32(selected ? ImGuiCol_HeaderActive : ImGuiCol_Border),
                                                4.0f);
        }
        else
        {
            ImGui::Button(iconForPath(path, isDir), ImVec2(iconSize, iconSize));
        }

        if (selected)
            ImGui::PopStyleColor();

        const bool hovered = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked())
            selectPath(ctx, path);
        if (isDir && hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            m_CurrentDir = path;

        const float textWidth = iconSize + 10.0f;
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + textWidth);
        ImGui::TextWrapped("%s", name.c_str());
        ImGui::PopTextWrapPos();

        ImGui::EndGroup();
        ImGui::NextColumn();
        ImGui::PopID();
    }

    void AssetBrowserWindow::selectPath(EditorContext& ctx, const std::filesystem::path& path)
    {
        if (!std::filesystem::exists(path))
            return;

        m_SelectedPath                  = path;
        ctx.state.selectedSourceAsset   = path.lexically_normal();
        Selection::clear(SelectionCategory::Entity);
        Selection::clear(SelectionCategory::Asset);

        if (std::filesystem::is_directory(path))
        {
            m_CurrentDir = path;
        }
    }
} // namespace vultra_app
