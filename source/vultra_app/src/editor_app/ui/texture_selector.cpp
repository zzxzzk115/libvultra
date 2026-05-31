#include "editor_app/ui/texture_selector.hpp"

#include "editor_app/asset_thumbnail_service.hpp"

#include <vultra/function/asset/builtin_assets.hpp>
#include <vultra/function/imgui/imgui_theme.hpp>
#include <vultra/function/services/asset_service.hpp>

#include <IconsMaterialDesignIcons.h>
#include <imgui.h>
#include <vasset/vasset_type.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <system_error>
#include <unordered_map>

namespace vultra_app::ui
{
    namespace
    {
        std::filesystem::path assetRoot(EditorContext& ctx)
        {
            return (ctx.state.currentProject / ctx.state.currentAssetRoot).lexically_normal();
        }

        std::string uriForPath(EditorContext& ctx, const std::filesystem::path& path)
        {
            std::error_code ec;
            const auto      rel = std::filesystem::relative(path.lexically_normal(), assetRoot(ctx), ec);
            if (ec || rel.empty())
                return {};
            return "res://" + rel.generic_string();
        }

        bool containsIgnoreCase(std::string_view text, std::string_view needle)
        {
            if (needle.empty())
                return true;

            const auto  lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
            std::string haystack(text);
            std::string query(needle);
            std::ranges::transform(haystack, haystack.begin(), lower);
            std::ranges::transform(query, query.begin(), lower);
            return haystack.find(query) != std::string::npos;
        }

        std::string trimLabel(const std::filesystem::path& path)
        {
            auto label = path.stem().generic_string();
            if (label.empty())
                label = path.filename().generic_string();
            return label;
        }

        std::string labelForTexture(const TextureSelection& texture)
        {
            if (!texture.label.empty())
                return texture.label;
            if (!texture.sourcePath.empty())
                return trimLabel(texture.sourcePath);
            auto label = std::filesystem::path(texture.uri).stem().generic_string();
            return label.empty() ? texture.uri : label;
        }

        std::unordered_map<std::string, vultra::CoreUUID> textureUuidBySourceUri(EditorContext& ctx)
        {
            std::unordered_map<std::string, vultra::CoreUUID> out;
            auto* assets = ctx.services ? ctx.services->tryGet<vultra::IAssetService>() : nullptr;
            if (!assets)
                return out;

            for (const auto& [uuidText, entry] : assets->registry().getRegistry())
            {
                if (entry.type != vasset::VAssetType::eTexture || entry.sourcePath.empty())
                    continue;
                vultra::CoreUUID uuid;
                vbase::UUID      parsed {};
                if (!vbase::try_parse_uuid(uuidText.c_str(), parsed))
                    continue;
                uuid = vultra::CoreUUID(parsed);
                if (!uuid.valid())
                    continue;
                out["res://" + std::filesystem::path(entry.sourcePath).generic_string()] = uuid;
            }
            return out;
        }

        std::vector<TextureSelection> collectBuiltinTextures();
        std::vector<TextureSelection> collectProjectTexturesUncached(EditorContext& ctx);

        void ensureTextureSelectorCache(EditorContext& ctx, TextureSelectorState& state)
        {
            if (!state.builtinCacheReady)
            {
                state.cachedBuiltinTextures = collectBuiltinTextures();
                state.builtinCacheReady     = true;
            }

            if (state.cachedProjectGeneration != ctx.state.projectGeneration)
            {
                state.cachedProjectTextures   = collectProjectTexturesUncached(ctx);
                state.cachedProjectGeneration = ctx.state.projectGeneration;
            }
        }

        std::vector<TextureSelection> cachedTextures(EditorContext& ctx, TextureSelectorState& state)
        {
            ensureTextureSelectorCache(ctx, state);
            std::vector<TextureSelection> out;
            out.reserve(state.cachedBuiltinTextures.size() + state.cachedProjectTextures.size());
            out.insert(out.end(), state.cachedBuiltinTextures.begin(), state.cachedBuiltinTextures.end());
            out.insert(out.end(), state.cachedProjectTextures.begin(), state.cachedProjectTextures.end());
            return out;
        }

        std::string textureUriForUuid(EditorContext& ctx, TextureSelectorState& state, const vultra::CoreUUID& uuid)
        {
            if (!uuid.valid())
                return {};
            ensureTextureSelectorCache(ctx, state);
            for (const auto& texture : state.cachedBuiltinTextures)
            {
                if (texture.uuid == uuid)
                    return texture.uri;
            }
            if (!ctx.services)
                return {};
            auto* assets = ctx.services->tryGet<vultra::IAssetService>();
            if (!assets)
                return {};
            const auto entry = assets->registry().lookup(uuid.native());
            if (entry.sourcePath.empty())
                return {};
            return "res://" + std::filesystem::path(entry.sourcePath).generic_string();
        }

        bool isLoadableBuiltinTexture(const std::filesystem::path& path)
        {
            const auto ext = path.extension().generic_string();
            return ext == ".vtexture" || ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" ||
                   ext == ".tga" || ext == ".hdr" || ext == ".pic" || ext == ".exr" || ext == ".ktx" ||
                   ext == ".ktx2" || ext == ".dds";
        }

        std::vector<TextureSelection> collectBuiltinTextures()
        {
            std::vector<TextureSelection> out;
            const auto                    root = (std::filesystem::path("builtin") / "textures").lexically_normal();
            std::error_code               ec;
            if (!std::filesystem::exists(root, ec) || ec)
                return out;

            for (const auto& entry : std::filesystem::recursive_directory_iterator(root, ec))
            {
                if (ec)
                    break;
                if (!entry.is_regular_file(ec) || ec)
                    continue;
                const auto path = entry.path().lexically_normal();
                if (!isLoadableBuiltinTexture(path))
                    continue;
                const auto rel = std::filesystem::relative(path, root, ec);
                if (ec || rel.empty())
                    continue;
                const auto uri = std::string(vultra::kBuiltinTextureUriPrefix) + rel.generic_string();
                out.push_back(TextureSelection {
                    .uri        = uri,
                    .label      = trimLabel(path),
                    .sourcePath = path,
                    .uuid       = vultra::builtinTextureUuidForUri(uri),
                });
            }

            std::ranges::sort(out,
                              [](const TextureSelection& lhs, const TextureSelection& rhs) { return lhs.uri < rhs.uri; });
            return out;
        }

        ImTextureID thumbnailForUri(EditorContext&        ctx,
                                    TextureSelectorState& state,
                                    std::string_view      uri,
                                    const float           iconSize)
        {
            static_cast<void>(iconSize);
            const bool cached    = state.previewCache.hasCachedTexturePreview(ctx, uri);
            const bool allowLoad = cached || state.remainingPreviewLoads > 0;
            auto       id        = state.previewCache.getTexturePreview(ctx, uri, allowLoad);
            if (!cached && id)
                --state.remainingPreviewLoads;
            return id;
        }

        ImTextureID thumbnailFor(EditorContext&               ctx,
                                 TextureSelectorState&        state,
                                 const std::filesystem::path& sourcePath,
                                 const float                  iconSize)
        {
            if (ctx.thumbnails)
            {
                const auto thumbnail = ctx.thumbnails->requestTexture(ctx, sourcePath);
                if (thumbnail.status == AssetThumbnailStatus::Ready)
                {
                    const bool cached    = state.previewCache.hasCachedImageFilePreview(ctx, thumbnail.outputPath);
                    const bool allowLoad = cached || state.remainingPreviewLoads > 0;
                    auto       id        = state.previewCache.getImageFilePreview(ctx, thumbnail.outputPath, allowLoad);
                    if (!cached && id)
                        --state.remainingPreviewLoads;
                    if (id)
                        return id;
                }
            }

            const bool cached    = state.previewCache.hasCachedTexturePreview(ctx, sourcePath);
            const bool allowLoad = cached || state.remainingPreviewLoads > 0;
            auto       id        = state.previewCache.getTexturePreview(ctx, sourcePath, allowLoad);
            if (!cached && id)
                --state.remainingPreviewLoads;
            return id;
        }

        TextureSelection textureSelectionForUri(EditorContext& ctx, TextureSelectorState& state, std::string_view uri)
        {
            if (uri.empty())
                return {};
            for (const auto& texture : cachedTextures(ctx, state))
            {
                if (texture.uri == uri)
                    return texture;
            }
            return {};
        }

        TextureSelection
        textureSelectionForUuid(EditorContext& ctx, TextureSelectorState& state, const vultra::CoreUUID& uuid)
        {
            if (!uuid.valid())
                return {};
            for (const auto& texture : cachedTextures(ctx, state))
            {
                if (texture.uuid == uuid)
                    return texture;
            }
            return {};
        }

        std::string ellipsizeText(std::string_view text, const float maxWidth)
        {
            if (text.empty() || maxWidth <= 0.0f)
                return {};

            std::string out {text};
            if (ImGui::CalcTextSize(out.c_str()).x <= maxWidth)
                return out;

            constexpr const char* ellipsis      = "...";
            const float           ellipsisWidth = ImGui::CalcTextSize(ellipsis).x;
            if (ellipsisWidth >= maxWidth)
                return ellipsis;

            while (!out.empty())
            {
                out.pop_back();
                std::string candidate = out + ellipsis;
                if (ImGui::CalcTextSize(candidate.c_str()).x <= maxWidth)
                    return candidate;
            }
            return ellipsis;
        }

        bool drawPreviewSelectorButton(const char*      id,
                                       const char*      fallbackIcon,
                                       ImTextureID      preview,
                                       std::string_view primary,
                                       std::string_view secondary,
                                       const ImVec2     size)
        {
            const ImVec2 pos     = ImGui::GetCursorScreenPos();
            const bool   pressed = ImGui::InvisibleButton(id, size);
            const bool   hovered = ImGui::IsItemHovered();
            const bool   held    = ImGui::IsItemActive();

            const ImVec4 fill     = held    ? vultra::imgui_theme::frameActive() :
                                    hovered ? vultra::imgui_theme::frameHovered() :
                                              vultra::imgui_theme::frame();
            auto*        drawList = ImGui::GetWindowDrawList();
            const ImVec2 max {pos.x + size.x, pos.y + size.y};
            drawList->AddRectFilled(pos, max, ImGui::GetColorU32(fill), 4.0f);
            drawList->AddRect(pos, max, ImGui::GetColorU32(vultra::imgui_theme::border()), 4.0f);

            const float  pad         = 4.0f;
            const float  previewSize = std::max(1.0f, size.y - pad * 2.0f);
            const ImVec2 imageMin {pos.x + pad, pos.y + pad};
            const ImVec2 imageMax {imageMin.x + previewSize, imageMin.y + previewSize};
            drawList->AddRectFilled(imageMin, imageMax, ImGui::GetColorU32(vultra::imgui_theme::panel()), 3.0f);
            if (preview)
            {
                drawList->AddImage(preview, imageMin, imageMax);
            }
            else
            {
                const ImVec2 iconSize = ImGui::CalcTextSize(fallbackIcon);
                drawList->AddText(ImVec2 {imageMin.x + (previewSize - iconSize.x) * 0.5f,
                                          imageMin.y + (previewSize - iconSize.y) * 0.5f},
                                  ImGui::GetColorU32(vultra::imgui_theme::textMuted()),
                                  fallbackIcon);
            }
            drawList->AddRect(imageMin, imageMax, ImGui::GetColorU32(vultra::imgui_theme::separator()), 3.0f);

            const float       textX     = imageMax.x + 8.0f;
            const float       textRight = max.x - 8.0f;
            const float       textWidth = std::max(1.0f, textRight - textX);
            const ImVec2      clipMin {textX, pos.y + 3.0f};
            const ImVec2      clipMax {textRight, max.y - 3.0f};
            const std::string primaryText    = primary.empty() ? std::string {"<none>"} : std::string {primary};
            const std::string primaryDisplay = ellipsizeText(primaryText, textWidth);
            drawList->PushClipRect(clipMin, clipMax, true);
            drawList->AddText(ImVec2 {textX, pos.y + 6.0f},
                              ImGui::GetColorU32(vultra::imgui_theme::text()),
                              primaryDisplay.c_str(),
                              primaryDisplay.c_str() + primaryDisplay.size());
            if (!secondary.empty())
            {
                const std::string secondaryDisplay = ellipsizeText(secondary, textWidth);
                drawList->AddText(ImVec2 {textX, pos.y + 23.0f},
                                  ImGui::GetColorU32(vultra::imgui_theme::textMuted()),
                                  secondaryDisplay.c_str(),
                                  secondaryDisplay.c_str() + secondaryDisplay.size());
            }
            drawList->PopClipRect();
            if (hovered && (!primary.empty() || !secondary.empty()))
            {
                if (secondary.empty())
                    ImGui::SetTooltip("%s", primaryText.c_str());
                else
                    ImGui::SetTooltip("%s\n%s", primaryText.c_str(), std::string(secondary).c_str());
            }
            return pressed;
        }
    } // namespace

    namespace
    {
        std::vector<TextureSelection> collectProjectTexturesUncached(EditorContext& ctx)
        {
            std::vector<TextureSelection> out;

            const auto      root = assetRoot(ctx);
            std::error_code ec;
            if (root.empty() || !std::filesystem::exists(root, ec) || ec)
                return out;

            auto uuidByUri = textureUuidBySourceUri(ctx);
            for (const auto& entry : std::filesystem::recursive_directory_iterator(root, ec))
            {
                if (ec)
                    break;
                if (!entry.is_regular_file(ec) || ec)
                    continue;
                const auto path = entry.path().lexically_normal();
                if (!isTextureSourceAsset(path))
                    continue;
                auto uri = uriForPath(ctx, path);
                if (uri.empty())
                    continue;
                TextureSelection selection;
                selection.uri        = std::move(uri);
                selection.label      = trimLabel(path);
                selection.sourcePath = path;
                if (auto it = uuidByUri.find(selection.uri); it != uuidByUri.end())
                    selection.uuid = it->second;
                out.push_back(std::move(selection));
            }

            std::ranges::sort(out,
                              [](const TextureSelection& lhs, const TextureSelection& rhs) { return lhs.uri < rhs.uri; });
            return out;
        }
    } // namespace

    std::vector<TextureSelection> collectProjectTextures(EditorContext& ctx)
    {
        auto out     = collectBuiltinTextures();
        auto project = collectProjectTexturesUncached(ctx);
        out.insert(out.end(), project.begin(), project.end());
        std::ranges::sort(out,
                          [](const TextureSelection& lhs, const TextureSelection& rhs) { return lhs.uri < rhs.uri; });
        return out;
    }

    bool drawTextureSelectorPopup(EditorContext&        ctx,
                                  const char*           popupId,
                                  TextureSelectorState& state,
                                  std::string_view      selectedUri,
                                  TextureSelection*     selected)
    {
        if (state.observedProjectGeneration != ctx.state.projectGeneration)
        {
            state.observedProjectGeneration = ctx.state.projectGeneration;
            state.previewCache.clear(ctx);
            state.remainingPreviewLoads = 16;
        }
        ensureTextureSelectorCache(ctx, state);

        bool changed = false;
        if (!ImGui::BeginPopup(popupId))
            return false;

        ImGui::SetNextItemWidth(std::max(180.0f, ImGui::GetContentRegionAvail().x - 170.0f));
        ImGui::InputTextWithHint("##TextureFilter", "Filter textures...", state.filter.data(), state.filter.size());
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::SliderFloat("Size", &state.iconSize, 48.0f, 128.0f, "%.0f px"))
            state.iconSize = std::clamp(state.iconSize, 48.0f, 128.0f);
        ImGui::Separator();

        state.remainingPreviewLoads = 24;
        const auto  textures        = cachedTextures(ctx, state);
        const float iconSize        = state.iconSize;
        const float cellWidth       = iconSize + 20.0f;
        const int   columns         = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / cellWidth));

        if (ImGui::BeginChild("##TextureSelectorGrid", ImVec2(0.0f, 320.0f), true))
        {
            ImGui::Columns(columns, nullptr, false);
            bool any = false;
            for (const auto& texture : textures)
            {
                const auto label = labelForTexture(texture);
                if (!containsIgnoreCase(texture.uri, state.filter.data()) &&
                    !containsIgnoreCase(label, state.filter.data()))
                    continue;

                any = true;
                ImGui::PushID(texture.uri.c_str());
                ImGui::BeginGroup();
                const bool   isSelected = texture.uri == selectedUri;
                const ImVec2 start      = ImGui::GetCursorScreenPos();
                auto previewId = texture.uri.starts_with("builtin://") ?
                                     thumbnailForUri(ctx, state, texture.uri, iconSize) :
                                     thumbnailFor(ctx, state, texture.sourcePath, iconSize);
                if (previewId)
                    ImGui::Image(previewId, ImVec2(iconSize, iconSize));
                else
                    ImGui::Button(ICON_MDI_IMAGE, ImVec2(iconSize, iconSize));

                const ImVec2 end {start.x + iconSize, start.y + iconSize};
                if (isSelected)
                    ImGui::GetWindowDrawList()->AddRect(
                        start, end, ImGui::GetColorU32(ImGuiCol_ButtonActive), 0.0f, 0, 2.0f);

                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                {
                    if (selected)
                        *selected = texture;
                    changed = true;
                    ImGui::CloseCurrentPopup();
                }

                if (ImGui::Selectable(
                        label.c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(iconSize, 0.0f)))
                {
                    if (selected)
                        *selected = texture;
                    changed = true;
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    ImGui::SetTooltip("%s", texture.uri.c_str());
                ImGui::EndGroup();
                ImGui::PopID();
                ImGui::NextColumn();
            }
            if (!any)
                ImGui::TextDisabled("No texture assets found.");
            ImGui::Columns(1);
        }
        ImGui::EndChild();

        if (ImGui::Button(ICON_MDI_CLOSE " Clear"))
        {
            if (selected)
                *selected = {};
            changed = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
        return changed;
    }

    bool drawTextureUriField(EditorContext& ctx, const char* label, std::string& uri, TextureSelectorState& state)
    {
        bool changed = false;
        ImGui::TextUnformatted(label);
        ImGui::PushID(label);
        const float clearButtonSize = ImGui::GetFrameHeight();
        const float fieldHeight     = 40.0f;
        const float width =
            std::max(1.0f, ImGui::GetContentRegionAvail().x - clearButtonSize - ImGui::GetStyle().ItemSpacing.x);
        changed |= drawTextureUriSelector(ctx, "TextureSelectorPopup", uri, state, ImVec2(width, fieldHeight));
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_MDI_CLOSE) && !uri.empty())
        {
            uri.clear();
            changed = true;
        }
        ImGui::PopID();
        return changed;
    }

    bool drawTextureUriSelector(EditorContext&        ctx,
                                const char*           popupId,
                                std::string&          uri,
                                TextureSelectorState& state,
                                const ImVec2          size)
    {
        if (state.observedProjectGeneration != ctx.state.projectGeneration)
        {
            state.observedProjectGeneration = ctx.state.projectGeneration;
            state.previewCache.clear(ctx);
            state.remainingPreviewLoads = 16;
        }
        ensureTextureSelectorCache(ctx, state);

        bool        changed   = false;
        auto        selection = textureSelectionForUri(ctx, state, uri);
        ImTextureID preview = selection.uri.starts_with("builtin://") ?
                                  thumbnailForUri(ctx, state, selection.uri, size.y) :
                                  selection.sourcePath.empty() ? ImTextureID {} :
                                                                  thumbnailFor(ctx, state, selection.sourcePath, size.y);
        const std::string labelText = selection.uri.empty() ? std::string(uri) : labelForTexture(selection);
        if (drawPreviewSelectorButton("##TextureSelectorField", ICON_MDI_IMAGE, preview, labelText, uri, size))
            ImGui::OpenPopup(popupId);

        TextureSelection popupSelection;
        if (drawTextureSelectorPopup(ctx, popupId, state, uri, &popupSelection))
        {
            uri     = popupSelection.uri;
            changed = true;
        }
        return changed;
    }

    bool
    drawTextureUuidField(EditorContext& ctx, const char* label, vultra::CoreUUID& uuid, TextureSelectorState& state)
    {
        if (state.observedProjectGeneration != ctx.state.projectGeneration)
        {
            state.observedProjectGeneration = ctx.state.projectGeneration;
            state.previewCache.clear(ctx);
            state.remainingPreviewLoads = 16;
        }
        ensureTextureSelectorCache(ctx, state);

        bool        changed = false;
        std::string uri     = textureUriForUuid(ctx, state, uuid);

        ImGui::TextUnformatted(label);
        ImGui::PushID(label);
        const float clearButtonSize = ImGui::GetFrameHeight();
        const float fieldHeight     = 40.0f;
        const float width =
            std::max(1.0f, ImGui::GetContentRegionAvail().x - clearButtonSize - ImGui::GetStyle().ItemSpacing.x);
        auto current = textureSelectionForUuid(ctx, state, uuid);
        if (uri.empty() && !current.uri.empty())
            uri = current.uri;
        ImTextureID preview = current.uri.starts_with("builtin://") ?
                                  thumbnailForUri(ctx, state, current.uri, fieldHeight) :
                                  current.sourcePath.empty() ? ImTextureID {} :
                                                               thumbnailFor(ctx, state, current.sourcePath, fieldHeight);
        const std::string labelText = current.uri.empty() ? std::string(uri) : labelForTexture(current);
        if (drawPreviewSelectorButton(
                "##TextureUuidField", ICON_MDI_IMAGE, preview, labelText, uri, ImVec2(width, fieldHeight)))
            ImGui::OpenPopup("TextureSelectorPopup");

        TextureSelection selection;
        if (drawTextureSelectorPopup(ctx, "TextureSelectorPopup", state, uri, &selection))
        {
            uuid    = selection.uuid;
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_MDI_CLOSE) && uuid.valid())
        {
            uuid    = {};
            changed = true;
        }
        ImGui::PopID();
        return changed;
    }
} // namespace vultra_app::ui
