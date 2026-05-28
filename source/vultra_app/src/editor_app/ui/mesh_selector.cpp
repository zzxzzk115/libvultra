#include "editor_app/ui/mesh_selector.hpp"

#include "editor_app/asset_thumbnail_service.hpp"

#include <vultra/function/imgui/imgui_theme.hpp>
#include <vultra/function/services/asset_service.hpp>

#include <IconsMaterialDesignIcons.h>
#include <imgui.h>
#include <vasset/vasset_type.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace vultra_app::ui
{
    namespace
    {
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

        bool parseUuid(std::string_view text, vultra::CoreUUID& out)
        {
            vbase::UUID parsed {};
            if (!vbase::try_parse_uuid(std::string(text).c_str(), parsed))
                return false;
            out = vultra::CoreUUID(parsed);
            return out.valid();
        }

        std::string meshNameFor(const vasset::VAssetRegistry::AssetEntry& entry)
        {
            auto name = std::filesystem::path(entry.importedPath).stem().generic_string();
            if (name.empty())
                name = std::filesystem::path(entry.sourcePath).stem().generic_string();
            if (name.empty())
                name = entry.importedPath;
            return name;
        }

        ImTextureID
        thumbnailFor(EditorContext& ctx, MeshSelectorState& state, const MeshSelection& mesh, const float iconSize)
        {
            static_cast<void>(iconSize);
            if (!ctx.thumbnails)
                return {};
            const auto thumbnail = ctx.thumbnails->requestMesh(ctx, mesh.uuid.toString(), mesh.importedPath);
            if (thumbnail.status != AssetThumbnailStatus::Ready)
                return {};

            const bool cached    = state.previewCache.hasCachedImageFilePreview(ctx, thumbnail.outputPath);
            const bool allowLoad = cached || state.remainingPreviewLoads > 0;
            auto       id        = state.previewCache.getImageFilePreview(ctx, thumbnail.outputPath, allowLoad);
            if (!cached && id)
                --state.remainingPreviewLoads;
            return id;
        }

        std::string meshUriForUuid(EditorContext& ctx, const vultra::CoreUUID& uuid)
        {
            if (!uuid.valid() || !ctx.services)
                return {};
            auto* assets = ctx.services->tryGet<vultra::IAssetService>();
            if (!assets)
                return {};
            std::string uri;
            return assets->resolver().resolve(uuid.native(), uri) ? uri : std::string {};
        }

        MeshSelection meshSelectionForUuid(EditorContext& ctx, const vultra::CoreUUID& uuid)
        {
            if (!uuid.valid())
                return {};
            for (const auto& mesh : collectProjectMeshes(ctx))
            {
                if (mesh.uuid == uuid)
                    return mesh;
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

    std::vector<MeshSelection> collectProjectMeshes(EditorContext& ctx)
    {
        std::vector<MeshSelection> out;
        auto*                      assets = ctx.services ? ctx.services->tryGet<vultra::IAssetService>() : nullptr;
        if (!assets)
            return out;

        for (const auto& [uuidText, entry] : assets->registry().getRegistry())
        {
            if (entry.type != vasset::VAssetType::eMesh || entry.importedPath.empty())
                continue;
            MeshSelection mesh;
            if (!parseUuid(uuidText, mesh.uuid))
                continue;
            mesh.name         = meshNameFor(entry);
            mesh.importedPath = entry.importedPath;
            out.push_back(std::move(mesh));
        }
        std::ranges::sort(out, [](const MeshSelection& lhs, const MeshSelection& rhs) {
            return lhs.name == rhs.name ? lhs.importedPath < rhs.importedPath : lhs.name < rhs.name;
        });
        return out;
    }

    bool drawMeshSelectorPopup(EditorContext&          ctx,
                               const char*             popupId,
                               MeshSelectorState&      state,
                               const vultra::CoreUUID& selected,
                               MeshSelection*          selection)
    {
        if (state.observedProjectGeneration != ctx.state.projectGeneration)
        {
            state.observedProjectGeneration = ctx.state.projectGeneration;
            state.previewCache.clear(ctx);
            state.remainingPreviewLoads = 16;
        }

        if (!ImGui::BeginPopup(popupId))
            return false;

        bool changed = false;
        ImGui::SetNextItemWidth(std::max(180.0f, ImGui::GetContentRegionAvail().x - 170.0f));
        ImGui::InputTextWithHint("##MeshFilter", "Filter meshes...", state.filter.data(), state.filter.size());
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::SliderFloat("Size", &state.iconSize, 48.0f, 128.0f, "%.0f px"))
            state.iconSize = std::clamp(state.iconSize, 48.0f, 128.0f);
        ImGui::Separator();

        state.remainingPreviewLoads = 24;
        const auto  meshes          = collectProjectMeshes(ctx);
        const float iconSize        = state.iconSize;
        const float cellWidth       = iconSize + 24.0f;
        const int   columns         = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / cellWidth));

        if (ImGui::BeginChild("##MeshSelectorGrid", ImVec2(0.0f, 320.0f), true))
        {
            ImGui::Columns(columns, nullptr, false);
            bool any = false;
            for (const auto& mesh : meshes)
            {
                if (!containsIgnoreCase(mesh.name, state.filter.data()) &&
                    !containsIgnoreCase(mesh.importedPath, state.filter.data()))
                    continue;

                any = true;
                ImGui::PushID(mesh.uuid.toString().c_str());
                ImGui::BeginGroup();
                const bool   isSelected = mesh.uuid == selected;
                const ImVec2 start      = ImGui::GetCursorScreenPos();
                if (auto preview = thumbnailFor(ctx, state, mesh, iconSize))
                    ImGui::Image(preview, ImVec2(iconSize, iconSize));
                else
                    ImGui::Button(ICON_MDI_CUBE_OUTLINE, ImVec2(iconSize, iconSize));

                const ImVec2 end {start.x + iconSize, start.y + iconSize};
                if (isSelected)
                    ImGui::GetWindowDrawList()->AddRect(
                        start, end, ImGui::GetColorU32(ImGuiCol_ButtonActive), 4.0f, 0, 2.0f);

                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                {
                    if (selection)
                        *selection = mesh;
                    changed = true;
                    ImGui::CloseCurrentPopup();
                }

                if (ImGui::Selectable(
                        mesh.name.c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(iconSize, 0.0f)))
                {
                    if (selection)
                        *selection = mesh;
                    changed = true;
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    ImGui::SetTooltip("%s", mesh.importedPath.c_str());
                ImGui::EndGroup();
                ImGui::PopID();
                ImGui::NextColumn();
            }
            if (!any)
                ImGui::TextDisabled("No imported mesh assets found.");
            ImGui::Columns(1);
        }
        ImGui::EndChild();

        if (ImGui::Button(ICON_MDI_CLOSE " Clear"))
        {
            if (selection)
                *selection = {};
            changed = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
        return changed;
    }

    bool drawMeshUuidField(EditorContext& ctx, const char* label, vultra::CoreUUID& uuid, MeshSelectorState& state)
    {
        bool       changed = false;
        const auto uri     = meshUriForUuid(ctx, uuid);
        ImGui::TextUnformatted(label);
        ImGui::PushID(label);
        const float fieldHeight     = 40.0f;
        const float clearButtonSize = ImGui::GetFrameHeight();
        const float width =
            std::max(1.0f, ImGui::GetContentRegionAvail().x - clearButtonSize - ImGui::GetStyle().ItemSpacing.x);
        const auto current = meshSelectionForUuid(ctx, uuid);
        const auto preview = current.uuid.valid() ? thumbnailFor(ctx, state, current, fieldHeight) : ImTextureID {};
        const std::string primary   = current.name.empty() ? std::string {} : current.name;
        const std::string secondary = uri.empty() ? current.importedPath : uri;
        if (drawPreviewSelectorButton(
                "##MeshField", ICON_MDI_CUBE_OUTLINE, preview, primary, secondary, ImVec2(width, fieldHeight)))
            ImGui::OpenPopup("MeshSelectorPopup");
        MeshSelection selection;
        if (drawMeshSelectorPopup(ctx, "MeshSelectorPopup", state, uuid, &selection))
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
