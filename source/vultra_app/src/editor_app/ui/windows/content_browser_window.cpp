#include "editor_app/ui/windows/content_browser_window.hpp"

#include "common/ui_widgets.hpp"
#include "editor_app/asset_thumbnail_service.hpp"
#include "editor_app/content_asset_registry.hpp"
#include "editor_app/editor_commands.hpp"
#include "editor_app/scene_thumbnail.hpp"
#include "editor_app/selection.hpp"

#include <IconsMaterialDesignIcons.h>
#include <ImGuiFileDialog/ImGuiFileDialog.h>
#include <vultra/core/base/common_context.hpp>
#include <imgui.h>
#include <vultra/function/services/asset_service.hpp>
#include <vultra/function/services/render_service.hpp>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
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
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
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

            auto ext = path.extension().generic_string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            if (ext == ".vmanifest" || ext == ".vimport" || ext == ".vpk" || ext == ".bin" || ext == ".log")
                return false;

            return true;
        }

        std::string lowerString(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
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
                   ext == ".comp" || ext == ".json" || ext == ".vproject" || ext == ".txt" || ext == ".mtl" ||
                   ext == ".md" || hasSuffix(name, ".vfeature.lua") || hasSuffix(name, ".vsrp.lua") ||
                   hasSuffix(name, ".vshaderlib.lua") || hasSuffix(name, ".vso.lua") || ext == ".vmatgraph" ||
                   hasSuffix(name, ".vmatgraph.json");
        }

        bool isSceneSourceAsset(const std::filesystem::path& path)
        {
            return lowerString(path.extension().generic_string()) == ".vscn";
        }

        bool isMaterialGraphSourceAsset(const std::filesystem::path& path)
        {
            const auto name = lowerString(path.filename().generic_string());
            const auto ext  = lowerString(path.extension().generic_string());
            return ext == ".vmatgraph" || hasSuffix(name, ".vmatgraph.json");
        }

        bool isRenderGraphSourceAsset(const std::filesystem::path& path)
        {
            return hasSuffix(lowerString(path.filename().generic_string()), ".vrg.json");
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
            const auto      root = (ctx.state.currentProject / ctx.state.currentAssetRoot).lexically_normal();
            std::error_code ec;
            const auto      rel     = std::filesystem::relative(path.lexically_normal(), root, ec);
            const auto      relText = rel.generic_string();
            if (ec || rel.empty() || relText == ".." || relText.starts_with("../"))
                return {};
            return "res://" + relText;
        }

        std::filesystem::path sceneThumbnailPathForAsset(const EditorContext& ctx, const std::filesystem::path& path)
        {
            const auto uri = pathToResUri(ctx, path);
            if (uri.empty())
                return {};
            return sceneThumbnailPath(ctx, uri);
        }

        std::vector<std::filesystem::directory_entry> sortedEntries(const std::filesystem::path& path)
        {
            std::vector<std::filesystem::directory_entry> entries;
            std::error_code                               ec;
            for (const auto& entry : std::filesystem::directory_iterator(path, ec))
                entries.push_back(entry);

            std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
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

            std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
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

        std::string sanitizeAssetFileName(std::string name)
        {
            name.erase(std::remove_if(name.begin(),
                                      name.end(),
                                      [](unsigned char ch) {
                                          return ch < 32 || ch == '<' || ch == '>' || ch == ':' || ch == '"' ||
                                                 ch == '/' || ch == '\\' || ch == '|' || ch == '?' || ch == '*';
                                      }),
                       name.end());

            while (!name.empty() && std::isspace(static_cast<unsigned char>(name.front())))
                name.erase(name.begin());
            while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back())))
                name.pop_back();
            return name;
        }

        void copyText(std::array<char, 128>& dst, std::string_view text)
        {
            const auto count = std::min(dst.size() - 1, text.size());
            std::memset(dst.data(), 0, dst.size());
            std::memcpy(dst.data(), text.data(), count);
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
            const auto      assetRoot = ctx.state.currentProject / ctx.state.currentAssetRoot;
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

        bool isModelSourceAsset(const std::filesystem::path& path)
        {
            auto ext = path.extension().generic_string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            return ext == ".gltf" || ext == ".glb" || ext == ".obj" || ext == ".fbx" || ext == ".dae";
        }

        bool isCookableTextureThumbnailSource(const std::filesystem::path& path)
        {
            auto ext = path.extension().generic_string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga" || ext == ".hdr";
        }

        bool setUuidDragPayload(const std::string& uuidText)
        {
            vbase::UUID parsed {};
            if (!vbase::try_parse_uuid(uuidText.c_str(), parsed))
                return false;
            const vultra::CoreUUID uuid(parsed);
            ImGui::SetDragDropPayload(kAssetUuidPayload, &uuid, sizeof(uuid));
            return true;
        }

        std::string unescapeManifestString(std::string value)
        {
            std::string out;
            out.reserve(value.size());
            bool escaped = false;
            for (const char ch : value)
            {
                if (escaped)
                {
                    out.push_back(ch);
                    escaped = false;
                }
                else if (ch == '\\')
                {
                    escaped = true;
                }
                else
                {
                    out.push_back(ch);
                }
            }
            return out;
        }

        std::unordered_map<std::string, std::string> readModelSubAssetNames(const std::filesystem::path& assetRoot,
                                                                            const std::string& manifestImportedPath)
        {
            std::unordered_map<std::string, std::string> names;
            std::ifstream                                in(assetRoot / std::filesystem::path(manifestImportedPath));
            if (!in)
                return names;

            std::string currentName;
            std::string line;
            while (std::getline(in, line))
            {
                if (line.starts_with("[node "))
                {
                    currentName.clear();
                    const auto marker = std::string_view(" name=\"");
                    const auto begin  = line.find(marker);
                    if (begin != std::string::npos)
                    {
                        const auto nameBegin = begin + marker.size();
                        auto       nameEnd   = nameBegin;
                        bool       escaped   = false;
                        while (nameEnd < line.size())
                        {
                            const char ch = line[nameEnd];
                            if (escaped)
                                escaped = false;
                            else if (ch == '\\')
                                escaped = true;
                            else if (ch == '"')
                                break;
                            ++nameEnd;
                        }
                        currentName = unescapeManifestString(line.substr(nameBegin, nameEnd - nameBegin));
                    }
                }
                else if (!currentName.empty() && line.starts_with("MeshComponent/mesh = \"res://"))
                {
                    const auto meshBegin = std::string_view("MeshComponent/mesh = \"res://").size();
                    const auto meshEnd   = line.find('"', meshBegin);
                    if (meshEnd != std::string::npos)
                        names.emplace(line.substr(meshBegin, meshEnd - meshBegin), currentName);
                }
            }

            return names;
        }

        std::vector<ModelSubAssetEntry> collectModelSubAssets(EditorContext& ctx, const std::filesystem::path& path)
        {
            std::vector<ModelSubAssetEntry> entries;
            if (!ctx.services || !isModelSourceAsset(path))
                return entries;

            auto* assetService = ctx.services->tryGet<vultra::IAssetService>();
            if (!assetService)
                return entries;

            vultra::CoreUUID sourceUuid;
            if (!resolveDraggableAsset(ctx, path, sourceUuid))
                return entries;

            const auto manifestEntry = assetService->registry().lookup(sourceUuid.native());
            if (manifestEntry.type != vasset::VAssetType::eSceneManifest || manifestEntry.importedPath.empty())
                return entries;

            const auto manifestStem = std::filesystem::path(manifestEntry.importedPath).filename().generic_string();
            const auto meshPrefix   = assetService->registry().getImportedFolderName() + "/mesh/" + manifestStem + "_";
            const auto assetRoot    = ctx.state.currentProject / ctx.state.currentAssetRoot;
            const auto names        = readModelSubAssetNames(assetRoot, manifestEntry.importedPath);

            for (const auto& [uuid, entry] : assetService->registry().getRegistry())
            {
                if (entry.type != vasset::VAssetType::eMesh || !entry.importedPath.starts_with(meshPrefix))
                    continue;

                auto nameIt = names.find(entry.sourcePath);
                if (nameIt == names.end())
                    nameIt = names.find(entry.importedPath);
                if (nameIt == names.end())
                    continue;

                auto name = nameIt->second;
                entries.push_back(ModelSubAssetEntry {
                    .uuid         = uuid,
                    .name         = std::move(name),
                    .importedPath = entry.importedPath,
                });
            }

            std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
                return a.importedPath < b.importedPath;
            });
            return entries;
        }

        void ensureModelSourceImported(EditorContext& ctx, const std::filesystem::path& path)
        {
            if (!ctx.services)
                return;
            auto* assetService = ctx.services->tryGet<vultra::IAssetService>();
            if (!assetService)
                return;
            const auto uri = sourceAssetUriFor(ctx, path);
            if (!uri.empty())
                (void)assetService->reimportAsset(uri, false);
        }

        void drawSubAssetDragSource(const std::string& uuid, const std::string& name, const std::string& importedPath)
        {
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
            {
                const bool valid = setUuidDragPayload(uuid);
                ImGui::TextUnformatted(ICON_MDI_CUBE_OUTLINE);
                ImGui::SameLine();
                ImGui::TextUnformatted(name.c_str());
                ImGui::TextDisabled("%s", importedPath.c_str());
                if (!valid)
                    ImGui::TextDisabled("Invalid sub asset uuid.");
                ImGui::EndDragDropSource();
            }
        }

        void drawSubAssetListFrame(const ImVec2& itemMin, const ImVec2& itemMax)
        {
            auto* drawList = ImGui::GetWindowDrawList();
            if (!drawList)
                return;

            const float  x0 = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMin().x + 8.0f;
            const float  x1 = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x - 8.0f;
            const ImVec2 min {x0, itemMin.y - 1.0f};
            const ImVec2 max {x1, itemMax.y + 1.0f};
            drawList->AddRect(min, max, IM_COL32(90, 145, 210, 90), 3.0f);
            drawList->AddLine(ImVec2(x0 + 6.0f, min.y), ImVec2(x0 + 6.0f, max.y), IM_COL32(90, 145, 210, 150), 2.0f);
        }

        std::string trimLine(std::string_view text)
        {
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
                text.remove_prefix(1);
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
                text.remove_suffix(1);
            return std::string(text);
        }

        std::string ellipsizeTextToWidth(std::string_view text, const float width)
        {
            constexpr std::string_view ellipsis = "...";
            if (width <= 0.0f)
                return {};
            if (ImGui::CalcTextSize(std::string(text).c_str()).x <= width)
                return std::string(text);
            if (ImGui::CalcTextSize(ellipsis.data(), ellipsis.data() + ellipsis.size()).x > width)
                return {};

            std::string out;
            for (size_t i = 1; i <= text.size(); ++i)
            {
                std::string candidate(text.substr(0, i));
                candidate += ellipsis;
                if (ImGui::CalcTextSize(candidate.c_str()).x > width)
                    break;
                out = std::move(candidate);
            }
            return out.empty() ? std::string(ellipsis) : out;
        }

        void drawWrappedEllipsizedLabel(std::string_view text, const float width, const int maxLines)
        {
            if (text.empty() || width <= 0.0f || maxLines <= 0)
                return;

            std::string remaining(text);
            int         drawnLines = 0;
            while (!remaining.empty() && drawnLines < maxLines)
            {
                if (drawnLines == maxLines - 1)
                {
                    const auto line = ellipsizeTextToWidth(trimLine(remaining), width);
                    if (!line.empty())
                        ImGui::TextUnformatted(line.c_str());
                    ++drawnLines;
                    break;
                }

                size_t fit       = 0;
                size_t lastBreak = std::string::npos;
                for (size_t i = 1; i <= remaining.size(); ++i)
                {
                    const char ch = remaining[i - 1];
                    if (std::isspace(static_cast<unsigned char>(ch)) || ch == '-' || ch == '_' || ch == '.')
                        lastBreak = i;

                    const auto candidate = remaining.substr(0, i);
                    if (ImGui::CalcTextSize(candidate.c_str()).x > width)
                        break;
                    fit = i;
                }

                if (fit >= remaining.size())
                {
                    const auto line = trimLine(remaining);
                    if (!line.empty())
                        ImGui::TextUnformatted(line.c_str());
                    ++drawnLines;
                    break;
                }

                const size_t cut =
                    lastBreak != std::string::npos && lastBreak > 0 && lastBreak <= fit ? lastBreak : fit;
                if (cut == 0)
                {
                    const auto line = ellipsizeTextToWidth(remaining, width);
                    if (!line.empty())
                        ImGui::TextUnformatted(line.c_str());
                    ++drawnLines;
                    break;
                }

                const auto line = trimLine(std::string_view(remaining).substr(0, cut));
                if (!line.empty())
                    ImGui::TextUnformatted(line.c_str());
                remaining.erase(0, cut);
                ++drawnLines;
            }

            const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
            while (drawnLines < maxLines)
            {
                ImGui::Dummy(ImVec2(width, lineHeight));
                ++drawnLines;
            }
        }

        void drawAssetDragSource(EditorContext& ctx, const std::filesystem::path& path)
        {
            if (!ctx.services)
                return;

            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
            {
                vultra::CoreUUID uuid;
                if (!resolveDraggableAsset(ctx, path, uuid))
                {
                    if (auto* assetService = ctx.services->tryGet<vultra::IAssetService>())
                    {
                        const auto uri = sourceAssetUriFor(ctx, path);
                        if (!uri.empty() && assetService->reimportAsset(uri, false))
                            (void)resolveDraggableAsset(ctx, path, uuid);
                    }
                }
                else if (isModelSourceAsset(path))
                {
                    if (auto* assetService = ctx.services->tryGet<vultra::IAssetService>())
                    {
                        const auto uri = sourceAssetUriFor(ctx, path);
                        if (!uri.empty() && assetService->reimportAsset(uri, false))
                            (void)resolveDraggableAsset(ctx, path, uuid);
                    }
                }

                if (uuid.valid())
                    ImGui::SetDragDropPayload(kAssetUuidPayload, &uuid, sizeof(uuid));
                ImGui::TextUnformatted(ui::sourceAssetIcon(path, false));
                ImGui::SameLine();
                ImGui::TextUnformatted(sourceAssetDisplayName(path, false).c_str());
                ImGui::TextDisabled("%s", sourceAssetUriFor(ctx, path).c_str());
                if (!uuid.valid())
                    ImGui::TextDisabled("Asset is not imported yet.");
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

        ImGuiFileDialogFlags importDialogFlags()
        {
            return ImGuiFileDialogFlags_Modal | ImGuiFileDialogFlags_HideColumnType |
                   ImGuiFileDialogFlags_HideColumnSize | ImGuiFileDialogFlags_HideColumnDate |
                   ImGuiFileDialogFlags_DontShowHiddenFiles | ImGuiFileDialogFlags_CaseInsensitiveExtentionFiltering |
                   ImGuiFileDialogFlags_NaturalSorting | ImGuiFileDialogFlags_DisableThumbnailMode;
        }

        std::filesystem::path uniqueImportDestination(const std::filesystem::path& dir,
                                                      const std::filesystem::path& source)
        {
            auto dst = dir / source.filename();
            if (!std::filesystem::exists(dst))
                return dst;

            const auto stem = source.stem().generic_string();
            const auto ext  = source.extension().generic_string();
            for (uint32_t i = 1; i < 10000; ++i)
            {
                dst = dir / (stem + "_" + std::to_string(i) + ext);
                if (!std::filesystem::exists(dst))
                    return dst;
            }
            return dir / (source.filename().generic_string() + "_copy");
        }

        bool copyExternalAssetIntoDirectory(const std::filesystem::path& source,
                                            const std::filesystem::path& targetDir,
                                            std::filesystem::path&       outPath,
                                            std::string&                 error)
        {
            std::error_code ec;
            const auto      normalizedSource = source.lexically_normal();
            const auto      normalizedTarget = targetDir.lexically_normal();
            if (!std::filesystem::exists(normalizedSource, ec))
            {
                error = "source does not exist";
                return false;
            }

            std::filesystem::create_directories(normalizedTarget, ec);
            if (ec)
            {
                error = ec.message();
                return false;
            }

            if (std::filesystem::equivalent(normalizedSource.parent_path(), normalizedTarget, ec))
            {
                outPath = normalizedSource;
                return true;
            }

            const auto dst = uniqueImportDestination(normalizedTarget, normalizedSource);
            if (std::filesystem::is_directory(normalizedSource, ec))
            {
                std::filesystem::copy(normalizedSource,
                                      dst,
                                      std::filesystem::copy_options::recursive |
                                          std::filesystem::copy_options::overwrite_existing,
                                      ec);
            }
            else
            {
                std::filesystem::copy_file(
                    normalizedSource, dst, std::filesystem::copy_options::overwrite_existing, ec);
            }
            if (ec)
            {
                error = ec.message();
                return false;
            }

            outPath = dst.lexically_normal();
            return true;
        }

        std::filesystem::path sourceAssetVImportPath(const std::filesystem::path& path)
        {
            auto sidecar = path;
            sidecar.replace_extension(".vimport");
            return sidecar;
        }

        bool deleteSourceAssetWithSidecars(const std::filesystem::path& path, std::error_code& ec)
        {
            std::error_code dirEc;
            if (std::filesystem::is_directory(path, dirEc))
            {
                std::filesystem::remove_all(path, ec);
                return !ec;
            }

            std::filesystem::remove(path, ec);
            if (ec)
                return false;

            const auto sidecar = sourceAssetVImportPath(path);
            if (sidecar != path && std::filesystem::exists(sidecar))
                std::filesystem::remove(sidecar, ec);
            return !ec;
        }
    } // namespace

    ContentBrowserWindow::ContentBrowserWindow() : EditorWindow("Content Browser", ICON_MDI_FOLDER_MULTIPLE_IMAGE)
    {
        registerBuiltinContentAssetCreators();
    }

    void ContentBrowserWindow::tick(EditorContext&) {}

    void ContentBrowserWindow::onClosed(EditorContext& ctx) { m_PreviewCache.clear(ctx); }

    void ContentBrowserWindow::onDestroy(EditorContext& ctx) { m_PreviewCache.clear(ctx); }

    void ContentBrowserWindow::draw(EditorContext& ctx)
    {
        syncAssetRoot(ctx);

        const bool visible =
            ImGui::Begin(title().c_str(), &m_Open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        if (!visible)
        {
            ImGui::End();
            return;
        }

        if (m_AssetRoot.empty())
        {
            ui::emptyState(
                ICON_MDI_FOLDER_OFF_OUTLINE, "No Asset Root", "Open or create a project to browse source assets.");
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
        const auto  assetRoot =
            state.currentProject.empty() ? std::filesystem::path {} : state.currentProject / state.currentAssetRoot;
        if (assetRoot == m_AssetRoot)
        {
            if (m_ObservedAssetFileGeneration != state.assetFileGeneration)
            {
                m_ObservedAssetFileGeneration = state.assetFileGeneration;
                invalidateEntryCache();
            }
            return;
        }

        m_PreviewCache.clear(ctx);
        m_AssetRoot                   = assetRoot.lexically_normal();
        m_CurrentDir                  = m_AssetRoot;
        m_ObservedAssetFileGeneration = state.assetFileGeneration;
        m_SelectedPath.clear();
        invalidateEntryCache();
    }

    void ContentBrowserWindow::drawDirectoryTree(const std::filesystem::path& path)
    {
        for (const auto& dir : directoryChildrenFor(path))
        {
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
            if (dir == m_CurrentDir)
                flags |= ImGuiTreeNodeFlags_Selected;

            const auto cacheKey = dir.lexically_normal().generic_string();
            auto       leafIt   = m_VisibleChildDirectoryCache.find(cacheKey);
            if (leafIt == m_VisibleChildDirectoryCache.end())
            {
                const bool hasChild = !directoryChildrenFor(dir).empty();
                leafIt              = m_VisibleChildDirectoryCache.emplace(cacheKey, hasChild).first;
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
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) &&
            !ctx.state.pendingExternalAssetDrops.empty())
        {
            auto dropped = std::move(ctx.state.pendingExternalAssetDrops);
            ctx.state.pendingExternalAssetDrops.clear();
            for (const auto& path : dropped)
                importExternalPath(ctx, path);
        }

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
            if (ImGui::MenuItem(ICON_MDI_FILE_IMPORT "  Import File"))
                openImportDialog(m_CurrentDir, false);
            if (ImGui::MenuItem(ICON_MDI_FOLDER_UPLOAD "  Import Folder"))
                openImportDialog(m_CurrentDir, true);
            ImGui::Separator();
            drawCreateAssetMenu(ctx, m_CurrentDir);
            if (ImGui::MenuItem(ICON_MDI_FOLDER_PLUS "  Create Folder"))
            {
                std::memset(m_NewFolderBuffer.data(), 0, m_NewFolderBuffer.size());
                std::strncpy(m_NewFolderBuffer.data(), "NewFolder", m_NewFolderBuffer.size() - 1);
                m_OpenNewFolderPopup = true;
            }
            ImGui::EndPopup();
        }

        const bool  listMode      = m_IconSize < m_ListThreshold;
        const auto& entries       = filteredEntriesForCurrentDir();
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
                {
                    drawListItem(ctx, entry);
                    if (isModelSourceAsset(entry) &&
                        m_ExpandedModelAssets.contains(entry.lexically_normal().generic_string()))
                    {
                        for (const auto& subAsset : modelSubAssetsFor(ctx, entry))
                            drawListSubAsset(ctx, entry, subAsset.uuid, subAsset.name, subAsset.importedPath);
                    }
                }

                ImGui::EndTable();
            }
            return;
        }

        const float  cellPadding = 12.0f;
        const float  cellWidth   = m_IconSize + cellPadding;
        const float  panelWidth  = std::max(1.0f, ImGui::GetContentRegionAvail().x);
        const ImVec2 gridMin     = ImGui::GetCursorScreenPos();
        const ImVec2 gridMax {gridMin.x + panelWidth, gridMin.y + ImGui::GetContentRegionAvail().y};
        const int    columns = std::max(1, static_cast<int>(panelWidth / cellWidth));

        ImGui::Columns(columns, nullptr, false);
        for (const auto& entry : entries)
        {
            drawGridItem(ctx, entry, m_IconSize);
            if (isModelSourceAsset(entry) && m_ExpandedModelAssets.contains(entry.lexically_normal().generic_string()))
            {
                for (const auto& subAsset : modelSubAssetsFor(ctx, entry))
                    drawGridSubAsset(ctx, entry, subAsset.uuid, subAsset.name, subAsset.importedPath, m_IconSize);
            }
        }
        ImGui::Columns(1);

        if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemActive() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            const ImVec2 mouse = ImGui::GetMousePos();
            const bool   overItem =
                std::any_of(m_GridItemBounds.begin(), m_GridItemBounds.end(), [&](const GridItemBounds& item) {
                    return mouse.x >= item.min.x && mouse.x <= item.max.x && mouse.y >= item.min.y &&
                           mouse.y <= item.max.y;
                });
            if (!overItem && mouse.x >= gridMin.x && mouse.x <= gridMax.x && mouse.y >= gridMin.y &&
                mouse.y <= gridMax.y)
                beginBoxSelection(mouse);
        }
        updateBoxSelection(ctx);
    }

    void ContentBrowserWindow::drawListItem(EditorContext& ctx, const std::filesystem::path& path)
    {
        std::error_code ec;
        const bool      isDir     = std::filesystem::is_directory(path, ec);
        const bool      isModel   = !isDir && isModelSourceAsset(path);
        const auto      expandKey = path.lexically_normal().generic_string();

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::PushID(path.generic_string().c_str());
        if (isModel)
        {
            const bool expanded = m_ExpandedModelAssets.contains(expandKey);
            if (ImGui::SmallButton(expanded ? ICON_MDI_CHEVRON_DOWN : ICON_MDI_CHEVRON_RIGHT))
            {
                if (expanded)
                    m_ExpandedModelAssets.erase(expandKey);
                else
                {
                    ensureModelSourceImported(ctx, path);
                    m_ExpandedModelAssets.insert(expandKey);
                }
            }
            ImGui::SameLine();
        }
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
        ImGui::PopID();
    }

    void ContentBrowserWindow::drawGridItem(EditorContext& ctx, const std::filesystem::path& path, float iconSize)
    {
        std::error_code ec;
        const bool      isDir   = std::filesystem::is_directory(path, ec);
        const bool      isModel = !isDir && isModelSourceAsset(path);
        const auto      name    = sourceAssetDisplayName(path, isDir);

        ImGui::PushID(path.generic_string().c_str());
        ImGui::BeginGroup();
        const float  tileWidth   = iconSize + 10.0f;
        const float  labelHeight = ImGui::GetTextLineHeight() * 2.0f;
        const float  tileHeight  = iconSize + labelHeight + 6.0f;
        const ImVec2 itemMin     = ImGui::GetCursorScreenPos();
        const ImVec2 itemMax {itemMin.x + tileWidth, itemMin.y + tileHeight};
        const bool   itemVisible = ImGui::IsRectVisible(itemMin, itemMax);

        if (!itemVisible)
        {
            ImGui::Dummy(ImVec2(tileWidth, tileHeight));
            ImGui::EndGroup();
            ImGui::NextColumn();
            ImGui::PopID();
            return;
        }

        const bool selected = isPathSelected(path);
        auto*      drawList = ImGui::GetWindowDrawList();

        ImGui::InvisibleButton("##TileHit", ImVec2(tileWidth, tileHeight), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        const bool tileHovered       = ImGui::IsItemHovered();
        const bool tileClicked       = ImGui::IsItemClicked(ImGuiMouseButton_Left);
        const bool tileDoubleClicked = tileHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
        const bool tilePopupOpen     = ImGui::BeginPopupContextItem("AssetGridContext");
        if (!isDir)
            drawAssetDragSource(ctx, path);
        ImGui::SetCursorScreenPos(itemMin);

        if (selected)
        {
            drawList->AddRectFilled(ImVec2 {itemMin.x - 3.0f, itemMin.y - 3.0f},
                                    ImVec2 {itemMax.x + 3.0f, itemMax.y + 2.0f},
                                    IM_COL32(28, 45, 62, 230),
                                    5.0f);
            drawList->AddRectFilled(ImVec2 {itemMin.x - 3.0f, itemMin.y - 3.0f},
                                    ImVec2 {itemMax.x + 3.0f, itemMin.y + 1.0f},
                                    IM_COL32(45, 145, 230, 230),
                                    5.0f,
                                    ImDrawFlags_RoundCornersTop);
        }

        ImTextureID previewId {};
        if (isDir)
        {
            previewId = m_PreviewCache.getBuiltinIcon(ctx, ui::BuiltinAssetIcon::Folder, iconSize);
        }
        else if (ui::isTextureSourceAsset(path))
        {
            if (ctx.thumbnails && isCookableTextureThumbnailSource(path))
            {
                const auto thumbnail = ctx.thumbnails->requestTexture(ctx, path);
                if (thumbnail.status == ui::AssetThumbnailStatus::Ready)
                {
                    const bool cached    = m_PreviewCache.hasCachedImageFilePreview(ctx, thumbnail.outputPath);
                    const bool allowLoad = cached || m_RemainingThumbnailLoads > 0;
                    previewId            = m_PreviewCache.getImageFilePreview(ctx, thumbnail.outputPath, allowLoad);
                    if (!cached && allowLoad)
                        --m_RemainingThumbnailLoads;
                }
            }
            else
            {
                const bool cached    = m_PreviewCache.hasCachedTexturePreview(ctx, path);
                const bool allowLoad = cached || m_RemainingThumbnailLoads > 0;
                previewId            = m_PreviewCache.getTexturePreview(ctx, path, allowLoad);
                if (!cached && allowLoad)
                    --m_RemainingThumbnailLoads;
            }
        }
        else if (isModel && ctx.thumbnails)
        {
            const auto thumbnail = ctx.thumbnails->requestModelRoot(ctx, path);
            if (thumbnail.status == ui::AssetThumbnailStatus::Ready)
            {
                const bool cached    = m_PreviewCache.hasCachedImageFilePreview(ctx, thumbnail.outputPath);
                const bool allowLoad = cached || m_RemainingThumbnailLoads > 0;
                previewId            = m_PreviewCache.getImageFilePreview(ctx, thumbnail.outputPath, allowLoad);
                if (!cached && allowLoad)
                    --m_RemainingThumbnailLoads;
            }
        }
        else if (isMaterialGraphSourceAsset(path) && ctx.thumbnails)
        {
            const auto thumbnail = ctx.thumbnails->requestMaterialGraph(ctx, path);
            if (thumbnail.status == ui::AssetThumbnailStatus::Ready)
            {
                const bool cached    = m_PreviewCache.hasCachedImageFilePreview(ctx, thumbnail.outputPath);
                const bool allowLoad = cached || m_RemainingThumbnailLoads > 0;
                previewId            = m_PreviewCache.getImageFilePreview(ctx, thumbnail.outputPath, allowLoad);
                if (!cached && allowLoad)
                    --m_RemainingThumbnailLoads;
            }
        }
        else if (isSceneSourceAsset(path))
        {
            const auto thumbnailPath = sceneThumbnailPathForAsset(ctx, path);
            std::error_code ec;
            if (!thumbnailPath.empty() && std::filesystem::exists(thumbnailPath, ec) && !ec)
            {
                const bool cached    = m_PreviewCache.hasCachedImageFilePreview(ctx, thumbnailPath);
                const bool allowLoad = cached || m_RemainingThumbnailLoads > 0;
                previewId            = m_PreviewCache.getImageFilePreview(ctx, thumbnailPath, allowLoad);
                if (!cached && allowLoad)
                    --m_RemainingThumbnailLoads;
            }
        }
        if (previewId)
        {
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

        const ImVec2 iconMin           = ImGui::GetItemRectMin();
        const ImVec2 iconMax           = ImGui::GetItemRectMax();
        const float  foldoutButtonSize = 14.0f;
        const ImVec2 foldoutButtonPos {
            iconMax.x - foldoutButtonSize - 6.0f,
            iconMin.y + (iconMax.y - iconMin.y - foldoutButtonSize) * 0.5f,
        };
        const ImVec2 foldoutButtonMax {
            foldoutButtonPos.x + foldoutButtonSize,
            foldoutButtonPos.y + foldoutButtonSize,
        };
        const ImVec2 mousePos       = ImGui::GetMousePos();
        const bool   mouseInFoldout = isModel && mousePos.x >= foldoutButtonPos.x && mousePos.x <= foldoutButtonMax.x &&
                                    mousePos.y >= foldoutButtonPos.y && mousePos.y <= foldoutButtonMax.y;
        if (!mouseInFoldout)
        {
            if (tileDoubleClicked)
                openPath(ctx, path);
            else if (tileClicked)
                selectPath(ctx, path);
        }
        if (tilePopupOpen)
        {
            drawContextMenu(ctx, path, isDir);
            ImGui::EndPopup();
        }

        if (isModel)
        {
            const auto expandKey = path.lexically_normal().generic_string();
            const bool expanded  = m_ExpandedModelAssets.contains(expandKey);
            if (mouseInFoldout && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                if (expanded)
                    m_ExpandedModelAssets.erase(expandKey);
                else
                {
                    ensureModelSourceImported(ctx, path);
                    m_ExpandedModelAssets.insert(expandKey);
                }
            }
            const bool  buttonHovered = mouseInFoldout;
            const bool  buttonActive  = mouseInFoldout && ImGui::IsMouseDown(ImGuiMouseButton_Left);
            auto*       drawList      = ImGui::GetWindowDrawList();
            const ImU32 buttonFill    = buttonActive  ? IM_COL32(32, 122, 214, 230) :
                                        buttonHovered ? IM_COL32(42, 145, 235, 210) :
                                        expanded      ? IM_COL32(35, 120, 205, 190) :
                                                        IM_COL32(28, 88, 150, 170);
            const ImU32 buttonBorder =
                buttonHovered || buttonActive ? IM_COL32(130, 195, 255, 235) : IM_COL32(72, 150, 225, 210);
            drawList->AddRectFilled(foldoutButtonPos, foldoutButtonMax, buttonFill, 4.0f);
            drawList->AddRect(foldoutButtonPos, foldoutButtonMax, buttonBorder, 4.0f);

            const ImU32 triangleColor =
                buttonHovered || buttonActive ? IM_COL32(255, 255, 255, 255) : IM_COL32(215, 235, 255, 255);
            const float  triW = 5.0f;
            const float  triH = 6.5f;
            const ImVec2 center {
                foldoutButtonPos.x + foldoutButtonSize * 0.5f,
                foldoutButtonPos.y + foldoutButtonSize * 0.5f,
            };
            if (expanded)
            {
                drawList->AddTriangleFilled(ImVec2(center.x - triH * 0.5f, center.y - triW * 0.35f),
                                            ImVec2(center.x + triH * 0.5f, center.y - triW * 0.35f),
                                            ImVec2(center.x, center.y + triW * 0.65f),
                                            triangleColor);
            }
            else
            {
                drawList->AddTriangleFilled(ImVec2(center.x - triW * 0.35f, center.y - triH * 0.5f),
                                            ImVec2(center.x - triW * 0.35f, center.y + triH * 0.5f),
                                            ImVec2(center.x + triW * 0.65f, center.y),
                                            triangleColor);
            }
        }

        const float textWidth = tileWidth;
        drawWrappedEllipsizedLabel(name, textWidth, 2);

        ImGui::EndGroup();
        m_GridItemBounds.push_back(GridItemBounds {
            .path = path,
            .min  = ImGui::GetItemRectMin(),
            .max  = ImGui::GetItemRectMax(),
        });
        ImGui::NextColumn();
        ImGui::PopID();
    }

    void ContentBrowserWindow::drawListSubAsset(EditorContext&               ctx,
                                                const std::filesystem::path& ownerPath,
                                                const std::string&           uuid,
                                                const std::string&           name,
                                                const std::string&           importedPath)
    {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::PushID((ownerPath.generic_string() + "#" + uuid).c_str());
        ImGui::Indent(22.0f);
        ImGui::Selectable((std::string(ICON_MDI_CUBE_OUTLINE) + "  " + name).c_str(),
                          Selection::lastCategory() == SelectionCategory::Asset &&
                              Selection::lastId().toString() == uuid,
                          ImGuiSelectableFlags_SpanAllColumns);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        {
            vultra::CoreUUID id;
            if (vbase::UUID parsed {}; vbase::try_parse_uuid(uuid.c_str(), parsed))
            {
                id = vultra::CoreUUID(parsed);
                Selection::select(SelectionCategory::Asset, id);
                ctx.state.selectedSourceAsset.clear();
            }
        }
        drawSubAssetListFrame(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        drawSubAssetDragSource(uuid, name, importedPath);
        ImGui::Unindent(22.0f);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Sub Mesh");
        ImGui::TableNextColumn();
        ImGui::TextWrapped("%s", importedPath.c_str());
        ImGui::PopID();
    }

    void ContentBrowserWindow::drawGridSubAsset(EditorContext&               ctx,
                                                const std::filesystem::path& ownerPath,
                                                const std::string&           uuid,
                                                const std::string&           name,
                                                const std::string&           importedPath,
                                                float                        iconSize)
    {
        ImGui::PushID((ownerPath.generic_string() + "#" + uuid).c_str());
        ImGui::BeginGroup();
        const ImVec2 itemMin = ImGui::GetCursorScreenPos();
        const ImVec2 itemMax {itemMin.x + iconSize + 10.0f,
                              itemMin.y + iconSize + ImGui::GetTextLineHeightWithSpacing() * 2.0f + 8.0f};
        const bool   itemVisible = ImGui::IsRectVisible(itemMin, itemMax);
        if (!itemVisible)
        {
            ImGui::Dummy(ImVec2(iconSize + 10.0f, iconSize + ImGui::GetTextLineHeightWithSpacing() * 2.0f + 8.0f));
            ImGui::EndGroup();
            ImGui::NextColumn();
            ImGui::PopID();
            return;
        }

        ImTextureID previewId {};
        if (ctx.thumbnails)
        {
            const auto thumbnail = ctx.thumbnails->requestMesh(ctx, uuid, importedPath);
            if (thumbnail.status == ui::AssetThumbnailStatus::Ready)
            {
                const bool cached    = m_PreviewCache.hasCachedImageFilePreview(ctx, thumbnail.outputPath);
                const bool allowLoad = cached || m_RemainingThumbnailLoads > 0;
                previewId            = m_PreviewCache.getImageFilePreview(ctx, thumbnail.outputPath, allowLoad);
                if (!cached && allowLoad)
                    --m_RemainingThumbnailLoads;
            }
        }

        if (previewId)
        {
            ImGui::Image(previewId, ImVec2(iconSize, iconSize));
            ImGui::GetWindowDrawList()->AddRect(
                ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(90, 145, 210, 150), 4.0f);
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.16f, 0.20f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.24f, 0.30f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.21f, 0.30f, 0.38f, 1.0f));
            ImGui::Button(ICON_MDI_CUBE_OUTLINE, ImVec2(iconSize, iconSize));
            ImGui::PopStyleColor(3);
            ImGui::GetWindowDrawList()->AddRect(
                ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(90, 145, 210, 150), 4.0f);
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        {
            if (vbase::UUID parsed {}; vbase::try_parse_uuid(uuid.c_str(), parsed))
            {
                Selection::select(SelectionCategory::Asset, vultra::CoreUUID(parsed));
                ctx.state.selectedSourceAsset.clear();
            }
        }
        drawSubAssetDragSource(uuid, name, importedPath);
        const float textWidth = iconSize + 10.0f;
        drawWrappedEllipsizedLabel(name, textWidth, 2);
        ImGui::EndGroup();
        m_GridItemBounds.push_back(GridItemBounds {
            .path = ownerPath,
            .min  = ImGui::GetItemRectMin(),
            .max  = ImGui::GetItemRectMax(),
        });
        ImGui::NextColumn();
        ImGui::PopID();
    }

    void
    ContentBrowserWindow::handleDeferredSelection(EditorContext& ctx, const std::filesystem::path& path, bool hovered)
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

        m_SelectedPath                = path;
        ctx.state.selectedSourceAsset = path.lexically_normal();
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
        else if (isMaterialGraphSourceAsset(path))
        {
            const auto uri = pathToResUri(ctx, path);
            if (uri.empty())
            {
                ctx.state.statusMessage = "Open material graph failed: graph is outside the asset root.";
                return;
            }

            queueOpenMaterialGraph(ctx.state, uri);
            ctx.state.statusMessage = "Opening material graph: " + uri;
        }
        else if (isRenderGraphSourceAsset(path))
        {
            const auto uri = pathToResUri(ctx, path);
            if (uri.empty())
            {
                ctx.state.statusMessage = "Open render graph failed: graph is outside the asset root.";
                return;
            }

            queueOpenRenderGraph(ctx.state, uri);
            ctx.state.statusMessage = "Opening render graph: " + uri;
        }
        else if (isCodeEditableSourceAsset(path))
        {
            requestOpenCodeEditor(ctx.state, path);
            ctx.state.statusMessage           = "Opened in Code Editor: " + path.filename().generic_string();
        }
        else if (isSceneSourceAsset(path))
        {
            const auto uri = pathToResUri(ctx, path);
            if (uri.empty())
            {
                ctx.state.statusMessage = "Open scene failed: scene is outside the asset root.";
                return;
            }

            queueOpenScene(ctx.state, uri);
            ctx.state.statusMessage = "Opening scene: " + uri;
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
        const ImVec2 min {std::min(m_BoxSelectStart.x, m_BoxSelectEnd.x),
                          std::min(m_BoxSelectStart.y, m_BoxSelectEnd.y)};
        const ImVec2 max {std::max(m_BoxSelectStart.x, m_BoxSelectEnd.x),
                          std::max(m_BoxSelectStart.y, m_BoxSelectEnd.y)};
        auto*        drawList = ImGui::GetForegroundDrawList(ImGui::GetWindowViewport());
        drawList->PushClipRect(ImGui::GetWindowPos(),
                               ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x,
                                      ImGui::GetWindowPos().y + ImGui::GetWindowSize().y),
                               true);
        drawList->AddRectFilled(min, max, IM_COL32(35, 110, 180, 54), 2.0f);
        drawList->AddRect(min, max, IM_COL32(80, 176, 255, 220), 2.0f, 0, 1.35f);
        drawList->PopClipRect();

        m_SelectedPaths.clear();
        for (const auto& item : m_GridItemBounds)
        {
            const bool intersects =
                item.max.x >= min.x && item.min.x <= max.x && item.max.y >= min.y && item.min.y <= max.y;
            if (intersects)
                m_SelectedPaths.push_back(item.path);
        }

        if (!m_SelectedPaths.empty())
        {
            m_SelectedPath                = m_SelectedPaths.back();
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
        m_DirectoryChildCache.clear();
        m_VisibleChildDirectoryCache.clear();
        m_ModelSubAssetCache.clear();
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
        if (ImGui::MenuItem(ICON_MDI_FILE_IMPORT "  Import File"))
            openImportDialog(isDirectory ? path : path.parent_path(), false);
        if (ImGui::MenuItem(ICON_MDI_FOLDER_UPLOAD "  Import Folder"))
            openImportDialog(isDirectory ? path : path.parent_path(), true);
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_MDI_FOLDER_PLUS "  Create Folder"))
        {
            m_CurrentDir = isDirectory ? path : path.parent_path();
            std::memset(m_NewFolderBuffer.data(), 0, m_NewFolderBuffer.size());
            std::strncpy(m_NewFolderBuffer.data(), "NewFolder", m_NewFolderBuffer.size() - 1);
            m_OpenNewFolderPopup = true;
        }
        drawCreateAssetMenu(ctx, isDirectory ? path : path.parent_path());
        if (ImGui::MenuItem(ICON_MDI_PENCIL "  Rename"))
        {
            m_RenamingPath = path;
            copyPathName(m_RenameBuffer, path);
            m_OpenRenamePopup = true;
        }
        if (ImGui::MenuItem(ICON_MDI_DELETE "  Delete"))
        {
            m_DeletePath      = path;
            m_OpenDeletePopup = true;
        }
    }

    void ContentBrowserWindow::drawCreateAssetMenu(EditorContext&, const std::filesystem::path& targetDir)
    {
        const auto& creators = ContentAssetRegistry::instance().creators();
        if (creators.empty())
            return;

        if (!ImGui::BeginMenu(ICON_MDI_PLUS_BOX_OUTLINE "  Create"))
            return;

        std::function<void(std::string_view)> drawLevel = [&](std::string_view prefix) {
            std::vector<std::string> openedMenus;
            for (const auto& creator : creators)
            {
                std::string_view path = creator.menuPath;
                if (!prefix.empty())
                {
                    if (!path.starts_with(prefix))
                        continue;
                    path.remove_prefix(prefix.size());
                }

                const auto slash = path.find('/');
                if (slash == std::string_view::npos)
                {
                    if (ImGui::MenuItem(path.data()))
                        openCreateAssetPopup(creator.id, targetDir);
                    continue;
                }

                const auto menu = std::string(path.substr(0, slash));
                if (std::find(openedMenus.begin(), openedMenus.end(), menu) != openedMenus.end())
                    continue;

                openedMenus.push_back(menu);
                if (ImGui::BeginMenu(menu.c_str()))
                {
                    const auto nextPrefix = std::string(prefix) + menu + "/";
                    drawLevel(nextPrefix);
                    ImGui::EndMenu();
                }
            }
        };

        drawLevel({});
        ImGui::EndMenu();
    }

    void ContentBrowserWindow::openCreateAssetPopup(const std::string& creatorId, const std::filesystem::path& targetDir)
    {
        const auto* creator = ContentAssetRegistry::instance().find(creatorId);
        if (!creator)
            return;

        m_CreateAssetCreatorId = creatorId;
        m_CreateAssetTargetDir = targetDir.empty() ? m_CurrentDir : targetDir;
        copyText(m_CreateAssetNameBuffer, creator->defaultFileName);
        m_OpenCreateAssetPopup = true;
    }

    bool ContentBrowserWindow::createRegisteredAsset(EditorContext& ctx)
    {
        const auto* creator = ContentAssetRegistry::instance().find(m_CreateAssetCreatorId);
        if (!creator)
        {
            ctx.state.statusMessage = "Create asset failed: unknown asset type.";
            return false;
        }

        auto fileName = sanitizeAssetFileName(m_CreateAssetNameBuffer.data());
        if (fileName.empty())
        {
            ctx.state.statusMessage = "Create asset failed: enter a file name.";
            return false;
        }

        if (!creator->extension.empty())
        {
            auto ext = std::filesystem::path(fileName).extension().generic_string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            auto expected = lowerString(creator->extension);
            if (ext != expected)
                fileName += creator->extension;
        }

        const auto targetDir = (m_CreateAssetTargetDir.empty() ? m_CurrentDir : m_CreateAssetTargetDir).lexically_normal();
        std::error_code relEc;
        const auto      relDir = std::filesystem::relative(targetDir, m_AssetRoot, relEc);
        const auto      relText = relDir.generic_string();
        if (relEc || relDir.empty() || relText == ".." || relText.starts_with("../"))
        {
            ctx.state.statusMessage = "Create asset failed: target is outside the asset root.";
            return false;
        }

        const auto target = (targetDir / fileName).lexically_normal();
        if (std::filesystem::exists(target))
        {
            ctx.state.statusMessage = "Create asset failed: file already exists.";
            return false;
        }

        std::error_code ec;
        std::filesystem::create_directories(target.parent_path(), ec);
        if (ec)
        {
            ctx.state.statusMessage = "Create asset failed: " + ec.message();
            return false;
        }

        const auto assetName = target.stem().generic_string();
        std::ofstream file(target, std::ios::trunc);
        if (!file)
        {
            ctx.state.statusMessage = "Create asset failed: cannot open file.";
            return false;
        }
        file << creator->makeText(assetName);
        file.close();
        if (!file)
        {
            ctx.state.statusMessage = "Create asset failed: cannot write file.";
            return false;
        }

        bool registered = false;
        if (ctx.services)
        {
            if (auto* assetService = ctx.services->tryGet<vultra::IAssetService>())
            {
                const auto uri = pathToResUri(ctx, target);
                if (!uri.empty())
                    registered = assetService->reimportAsset(uri, false);
            }
        }

        ++ctx.state.assetFileGeneration;
        invalidateEntryCache();
        m_CurrentDir = targetDir;
        selectPath(ctx, target);
        if (creator->openInCodeEditor)
            openPath(ctx, target);

        ctx.state.statusMessage = registered ? "Created " + creator->displayName + "." :
                                             "Created " + creator->displayName +
                                                 ", but asset registry import did not run.";
        return true;
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

        if (m_OpenCreateAssetPopup)
        {
            ImGui::OpenPopup("Create Asset");
            m_OpenCreateAssetPopup = false;
        }
        if (ImGui::BeginPopupModal("Create Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            const auto* creator = ContentAssetRegistry::instance().find(m_CreateAssetCreatorId);
            ImGui::TextWrapped("%s", m_CreateAssetTargetDir.generic_string().c_str());
            ImGui::InputText("Name", m_CreateAssetNameBuffer.data(), m_CreateAssetNameBuffer.size());
            if (creator && !creator->extension.empty())
            {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", creator->extension.c_str());
            }
            if (ImGui::Button("Create"))
            {
                if (createRegisteredAsset(ctx))
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
                deleteSourceAssetWithSidecars(m_DeletePath, ec);
                ctx.state.statusMessage = ec ? "Delete failed: " + ec.message() : "Deleted asset.";
                if (m_SelectedPath == m_DeletePath)
                {
                    m_SelectedPath.clear();
                    ctx.state.selectedSourceAsset.clear();
                }
                if (!ec)
                {
                    ctx.state.pendingAssetImportPaths.push_back(m_DeletePath);
                    ctx.state.pendingAssetImportRefresh = true;
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

        drawImportDialogs(ctx);
    }

    void ContentBrowserWindow::openImportDialog(const std::filesystem::path& targetDir, const bool directory)
    {
        m_ImportTargetDir = targetDir.empty() ? m_CurrentDir : targetDir;

        IGFD::FileDialogConfig config;
        config.path  = m_ImportTargetDir.empty() ? "." : m_ImportTargetDir.generic_string();
        config.flags = importDialogFlags();
        ImGuiFileDialog::Instance()->OpenDialog(directory ? "ContentBrowserImportFolder" : "ContentBrowserImportFile",
                                                directory ? "Import Folder" : "Import File",
                                                directory ? nullptr : ".*",
                                                config);
    }

    void ContentBrowserWindow::drawImportDialogs(EditorContext& ctx)
    {
        ui::ScopedPopupStyle style;
        constexpr ImVec2     dialogSize {640.0f, 420.0f};

        if (ImGuiFileDialog::Instance()->Display(
                "ContentBrowserImportFile", ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings, dialogSize))
        {
            if (ImGuiFileDialog::Instance()->IsOk())
                importExternalPath(
                    ctx,
                    std::filesystem::path(ImGuiFileDialog::Instance()->GetFilePathName(IGFD_ResultMode_KeepInputFile)));
            ImGuiFileDialog::Instance()->Close();
        }

        if (ImGuiFileDialog::Instance()->Display("ContentBrowserImportFolder",
                                                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings,
                                                 dialogSize))
        {
            if (ImGuiFileDialog::Instance()->IsOk())
            {
                auto selected =
                    std::filesystem::path(ImGuiFileDialog::Instance()->GetFilePathName(IGFD_ResultMode_KeepInputFile));
                if (selected.empty())
                    selected = std::filesystem::path(ImGuiFileDialog::Instance()->GetCurrentPath());
                importExternalPath(ctx, selected);
            }
            ImGuiFileDialog::Instance()->Close();
        }
    }

    void ContentBrowserWindow::importExternalPath(EditorContext& ctx, const std::filesystem::path& source)
    {
        if (source.empty())
            return;

        std::filesystem::path copiedPath;
        std::string           error;
        if (!copyExternalAssetIntoDirectory(
                source, m_ImportTargetDir.empty() ? m_CurrentDir : m_ImportTargetDir, copiedPath, error))
        {
            ctx.state.statusMessage = "Import failed: " + error;
            return;
        }

        ctx.state.pendingAssetImportPaths.push_back(copiedPath);
        ctx.state.pendingAssetImportRefresh = true;
        ctx.state.statusMessage             = "Copied asset. Import queued.";
        invalidateEntryCache();
        m_CurrentDir   = (m_ImportTargetDir.empty() ? m_CurrentDir : m_ImportTargetDir).lexically_normal();
        m_SelectedPath = copiedPath;
        ctx.state.selectedSourceAsset = copiedPath;
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

        m_CachedFilter = filter;
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

    const std::vector<std::filesystem::path>&
    ContentBrowserWindow::directoryChildrenFor(const std::filesystem::path& path)
    {
        const auto key = path.lexically_normal().generic_string();
        auto       it  = m_DirectoryChildCache.find(key);
        if (it != m_DirectoryChildCache.end())
            return it->second;

        std::vector<std::filesystem::path> children;
        std::error_code                    ec;
        for (const auto& entry : std::filesystem::directory_iterator(path, ec))
        {
            if (ec)
                break;

            std::error_code entryEc;
            if (!entry.is_directory(entryEc) || entryEc)
                continue;

            const auto dir = entry.path();
            if (isEditorVisibleSourceAsset(m_AssetRoot, dir))
                children.push_back(dir.lexically_normal());
        }

        std::sort(children.begin(), children.end(), [](const auto& a, const auto& b) {
            return a.generic_string() < b.generic_string();
        });
        return m_DirectoryChildCache.emplace(key, std::move(children)).first->second;
    }

    const std::vector<ModelSubAssetEntry>& ContentBrowserWindow::modelSubAssetsFor(EditorContext&               ctx,
                                                                                   const std::filesystem::path& path)
    {
        if (m_ModelSubAssetCacheGeneration != ctx.state.projectGeneration)
        {
            m_ModelSubAssetCache.clear();
            m_ModelSubAssetCacheGeneration = ctx.state.projectGeneration;
        }

        const auto key = path.lexically_normal().generic_string();
        auto       it  = m_ModelSubAssetCache.find(key);
        if (it == m_ModelSubAssetCache.end())
            it = m_ModelSubAssetCache.emplace(key, collectModelSubAssets(ctx, path)).first;
        return it->second;
    }
} // namespace vultra_app
