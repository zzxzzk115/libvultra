#include "editor_app/ui/texture_selector.hpp"

#include "editor_app/asset_thumbnail_service.hpp"

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
            const auto rel = std::filesystem::relative(path.lexically_normal(), assetRoot(ctx), ec);
            if (ec || rel.empty())
                return {};
            return "res://" + rel.generic_string();
        }

        bool containsIgnoreCase(std::string_view text, std::string_view needle)
        {
            if (needle.empty())
                return true;

            const auto lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
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
                vbase::UUID parsed {};
                if (!vbase::try_parse_uuid(uuidText.c_str(), parsed))
                    continue;
                uuid = vultra::CoreUUID(parsed);
                if (!uuid.valid())
                    continue;
                out["res://" + std::filesystem::path(entry.sourcePath).generic_string()] = uuid;
            }
            return out;
        }

        std::string textureUriForUuid(EditorContext& ctx, const vultra::CoreUUID& uuid)
        {
            if (!uuid.valid() || !ctx.services)
                return {};
            auto* assets = ctx.services->tryGet<vultra::IAssetService>();
            if (!assets)
                return {};
            const auto entry = assets->registry().lookup(uuid.native());
            if (entry.sourcePath.empty())
                return {};
            return "res://" + std::filesystem::path(entry.sourcePath).generic_string();
        }

        ImTextureID thumbnailFor(EditorContext& ctx,
                                 TextureSelectorState& state,
                                 const std::filesystem::path& sourcePath,
                                 const float iconSize)
        {
            if (ctx.thumbnails)
            {
                const auto thumbnail = ctx.thumbnails->requestTexture(ctx, sourcePath);
                if (thumbnail.status == AssetThumbnailStatus::Ready)
                {
                    const bool cached = state.previewCache.hasCachedImageFilePreview(ctx, thumbnail.outputPath);
                    const bool allowLoad = cached || state.remainingPreviewLoads > 0;
                    auto id = state.previewCache.getImageFilePreview(ctx, thumbnail.outputPath, allowLoad);
                    if (!cached && id)
                        --state.remainingPreviewLoads;
                    if (id)
                        return id;
                }
            }

            const bool cached = state.previewCache.hasCachedTexturePreview(ctx, sourcePath);
            const bool allowLoad = cached || state.remainingPreviewLoads > 0;
            auto id = state.previewCache.getTexturePreview(ctx, sourcePath, allowLoad);
            if (!cached && id)
                --state.remainingPreviewLoads;
            return id;
        }
    } // namespace

    std::vector<TextureSelection> collectProjectTextures(EditorContext& ctx)
    {
        std::vector<TextureSelection> out;
        const auto root = assetRoot(ctx);
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
            selection.uri = std::move(uri);
            selection.sourcePath = path;
            if (auto it = uuidByUri.find(selection.uri); it != uuidByUri.end())
                selection.uuid = it->second;
            out.push_back(std::move(selection));
        }

        std::ranges::sort(out, [](const TextureSelection& lhs, const TextureSelection& rhs) {
            return lhs.uri < rhs.uri;
        });
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
        const auto textures = collectProjectTextures(ctx);
        const float iconSize = state.iconSize;
        const float cellWidth = iconSize + 20.0f;
        const int columns = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / cellWidth));

        if (ImGui::BeginChild("##TextureSelectorGrid", ImVec2(0.0f, 320.0f), true))
        {
            ImGui::Columns(columns, nullptr, false);
            bool any = false;
            for (const auto& texture : textures)
            {
                const auto label = trimLabel(texture.sourcePath);
                if (!containsIgnoreCase(texture.uri, state.filter.data()) && !containsIgnoreCase(label, state.filter.data()))
                    continue;

                any = true;
                ImGui::PushID(texture.uri.c_str());
                ImGui::BeginGroup();
                const bool isSelected = texture.uri == selectedUri;
                const ImVec2 start = ImGui::GetCursorScreenPos();
                auto previewId = thumbnailFor(ctx, state, texture.sourcePath, iconSize);
                if (previewId)
                    ImGui::Image(previewId, ImVec2(iconSize, iconSize));
                else
                    ImGui::Button(ICON_MDI_IMAGE, ImVec2(iconSize, iconSize));

                const ImVec2 end {start.x + iconSize, start.y + iconSize};
                if (isSelected)
                    ImGui::GetWindowDrawList()->AddRect(start, end, ImGui::GetColorU32(ImGuiCol_ButtonActive), 0.0f, 0, 2.0f);

                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                {
                    if (selected)
                        *selected = texture;
                    changed = true;
                    ImGui::CloseCurrentPopup();
                }

                if (ImGui::Selectable(label.c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(iconSize, 0.0f)))
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
        const float buttonSize = ImGui::GetFrameHeight();
        const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x - buttonSize - ImGui::GetStyle().ItemSpacing.x);
        if (ImGui::Button(uri.empty() ? "<none>" : uri.c_str(), ImVec2(width, 0.0f)))
            ImGui::OpenPopup("TextureSelectorPopup");

        TextureSelection selection;
        if (drawTextureSelectorPopup(ctx, "TextureSelectorPopup", state, uri, &selection))
        {
            uri = selection.uri;
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_MDI_CLOSE) && !uri.empty())
        {
            uri.clear();
            changed = true;
        }
        ImGui::PopID();
        return changed;
    }

    bool drawTextureUuidField(EditorContext& ctx, const char* label, vultra::CoreUUID& uuid, TextureSelectorState& state)
    {
        bool changed = false;
        std::string uri = textureUriForUuid(ctx, uuid);

        ImGui::TextUnformatted(label);
        ImGui::PushID(label);
        const float buttonSize = ImGui::GetFrameHeight();
        const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x - buttonSize - ImGui::GetStyle().ItemSpacing.x);
        if (ImGui::Button(uri.empty() ? "<none>" : uri.c_str(), ImVec2(width, 0.0f)))
            ImGui::OpenPopup("TextureSelectorPopup");

        TextureSelection selection;
        if (drawTextureSelectorPopup(ctx, "TextureSelectorPopup", state, uri, &selection))
        {
            uuid = selection.uuid;
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_MDI_CLOSE) && uuid.valid())
        {
            uuid = {};
            changed = true;
        }
        ImGui::PopID();
        return changed;
    }
} // namespace vultra_app::ui
