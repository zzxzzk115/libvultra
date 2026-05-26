#include "editor_app/ui/mesh_selector.hpp"

#include "editor_app/asset_thumbnail_service.hpp"

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
            const auto lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
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

        ImTextureID thumbnailFor(EditorContext& ctx,
                                 MeshSelectorState& state,
                                 const MeshSelection& mesh,
                                 const float iconSize)
        {
            static_cast<void>(iconSize);
            if (!ctx.thumbnails)
                return {};
            const auto thumbnail = ctx.thumbnails->requestMesh(ctx, mesh.uuid.toString(), mesh.importedPath);
            if (thumbnail.status != AssetThumbnailStatus::Ready)
                return {};

            const bool cached = state.previewCache.hasCachedImageFilePreview(ctx, thumbnail.outputPath);
            const bool allowLoad = cached || state.remainingPreviewLoads > 0;
            auto id = state.previewCache.getImageFilePreview(ctx, thumbnail.outputPath, allowLoad);
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
    } // namespace

    std::vector<MeshSelection> collectProjectMeshes(EditorContext& ctx)
    {
        std::vector<MeshSelection> out;
        auto* assets = ctx.services ? ctx.services->tryGet<vultra::IAssetService>() : nullptr;
        if (!assets)
            return out;

        for (const auto& [uuidText, entry] : assets->registry().getRegistry())
        {
            if (entry.type != vasset::VAssetType::eMesh || entry.importedPath.empty())
                continue;
            MeshSelection mesh;
            if (!parseUuid(uuidText, mesh.uuid))
                continue;
            mesh.name = meshNameFor(entry);
            mesh.importedPath = entry.importedPath;
            out.push_back(std::move(mesh));
        }
        std::ranges::sort(out, [](const MeshSelection& lhs, const MeshSelection& rhs) {
            return lhs.name == rhs.name ? lhs.importedPath < rhs.importedPath : lhs.name < rhs.name;
        });
        return out;
    }

    bool drawMeshSelectorPopup(EditorContext&       ctx,
                               const char*          popupId,
                               MeshSelectorState&   state,
                               const vultra::CoreUUID& selected,
                               MeshSelection*       selection)
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
        const auto meshes = collectProjectMeshes(ctx);
        const float iconSize = state.iconSize;
        const float cellWidth = iconSize + 24.0f;
        const int columns = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / cellWidth));

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
                const bool isSelected = mesh.uuid == selected;
                const ImVec2 start = ImGui::GetCursorScreenPos();
                if (auto preview = thumbnailFor(ctx, state, mesh, iconSize))
                    ImGui::Image(preview, ImVec2(iconSize, iconSize));
                else
                    ImGui::Button(ICON_MDI_CUBE_OUTLINE, ImVec2(iconSize, iconSize));

                const ImVec2 end {start.x + iconSize, start.y + iconSize};
                if (isSelected)
                    ImGui::GetWindowDrawList()->AddRect(start, end, ImGui::GetColorU32(ImGuiCol_ButtonActive), 4.0f, 0, 2.0f);

                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                {
                    if (selection)
                        *selection = mesh;
                    changed = true;
                    ImGui::CloseCurrentPopup();
                }

                if (ImGui::Selectable(mesh.name.c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(iconSize, 0.0f)))
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
        bool changed = false;
        const auto uri = meshUriForUuid(ctx, uuid);
        ImGui::TextUnformatted(label);
        ImGui::PushID(label);
        const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x);
        if (ImGui::Button(uri.empty() ? "<sphere>" : uri.c_str(), ImVec2(width, 0.0f)))
            ImGui::OpenPopup("MeshSelectorPopup");
        MeshSelection selection;
        if (drawMeshSelectorPopup(ctx, "MeshSelectorPopup", state, uuid, &selection))
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
