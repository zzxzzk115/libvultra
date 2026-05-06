#include "editor_app/ui/windows/inspector_window.hpp"

#include "editor_app/selection.hpp"

#include <vultra/function/services/asset_service.hpp>
#include <vultra/function/services/world_service.hpp>
#include <vultra/function/world/components/gaussian_splat_component.hpp>
#include <vultra/function/world/components/hierarchy_component.hpp>
#include <vultra/function/world/components/id_component.hpp>
#include <vultra/function/world/components/mesh_component.hpp>
#include <vultra/function/world/components/name_component.hpp>
#include <vultra/function/world/components/prefab_instance_component.hpp>
#include <vultra/function/world/components/script_component.hpp>
#include <vultra/function/world/components/transform_component.hpp>
#include <vultra/function/world/world.hpp>

#include <glm/gtc/quaternion.hpp>
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <string>

namespace vultra_app
{
    namespace
    {
        entt::entity findEntityByUUID(vultra::World& world, const vultra::CoreUUID& uuid)
        {
            auto& reg  = world.registry();
            auto  view = reg.view<vultra::IDComponent>();
            for (auto e : view)
            {
                if (view.get<vultra::IDComponent>(e).uuid == uuid)
                    return e;
            }
            return entt::null;
        }

        template<typename Component>
        bool componentHeader(const char* name, vultra::World& world, entt::entity entity)
        {
            return world.registry().all_of<Component>(entity) &&
                   ImGui::CollapsingHeader(name, ImGuiTreeNodeFlags_DefaultOpen);
        }

        void copyName(std::array<char, 128>& dst, const std::string& src)
        {
            const auto count = std::min(dst.size() - 1, src.size());
            std::memcpy(dst.data(), src.data(), count);
            dst[count] = '\0';
        }

        bool sourceAssetHasExtension(const std::filesystem::path& path, std::initializer_list<const char*> exts)
        {
            auto ext = path.extension().generic_string();
            std::transform(ext.begin(),
                           ext.end(),
                           ext.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            return std::any_of(exts.begin(), exts.end(), [&](const char* candidate) { return ext == candidate; });
        }

        void drawImagePreviewPlaceholder(const std::filesystem::path& path, const char* note)
        {
            ImGui::TextUnformatted("Preview");
            const float size = std::min(ImGui::GetContentRegionAvail().x, 220.0f);
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            ImDrawList*  dl  = ImGui::GetWindowDrawList();
            const ImVec2 max {pos.x + size, pos.y + size};

            const float tile = 16.0f;
            for (float y = pos.y; y < max.y; y += tile)
            {
                for (float x = pos.x; x < max.x; x += tile)
                {
                    const bool dark = (static_cast<int>((x - pos.x) / tile) + static_cast<int>((y - pos.y) / tile)) % 2 == 0;
                    dl->AddRectFilled(ImVec2(x, y),
                                      ImVec2(std::min(x + tile, max.x), std::min(y + tile, max.y)),
                                      dark ? IM_COL32(62, 66, 72, 255) : IM_COL32(82, 88, 96, 255));
                }
            }
            dl->AddRect(pos, max, IM_COL32(150, 160, 175, 255), 6.0f, 0, 1.5f);
            const auto label = path.filename().generic_string();
            const auto text  = ImGui::CalcTextSize(label.c_str());
            dl->AddText(ImVec2(pos.x + (size - text.x) * 0.5f, pos.y + (size - text.y) * 0.5f),
                        IM_COL32(235, 238, 242, 255),
                        label.c_str());
            ImGui::Dummy(ImVec2(size, size));
            ImGui::TextDisabled("%s", note);
        }
    } // namespace

    InspectorWindow::InspectorWindow() : EditorWindow("Inspector") {}

    void InspectorWindow::draw(EditorContext& ctx)
    {
        ImGui::Begin(m_Name.c_str(), &m_Open);

        if (Selection::lastCategory() == SelectionCategory::Entity)
            drawEntityInspector(ctx);
        else if (Selection::lastCategory() == SelectionCategory::Asset)
            drawAssetInspector(ctx);
        else if (!ctx.state.selectedSourceAsset.empty())
            drawSourceAssetInspector(ctx);
        else
        {
            auto& state = ctx.state;
            ImGui::TextUnformatted("Project");
            ImGui::Separator();
            ImGui::TextWrapped("Name: %s", state.currentProjectName.empty() ? "(blank session)" : state.currentProjectName.c_str());
            ImGui::TextWrapped("Root: %s", state.currentProject.empty() ? "(none)" : state.currentProject.generic_string().c_str());
            ImGui::TextWrapped("Asset root: %s", state.currentAssetRoot.c_str());
            ImGui::TextWrapped("Default scene: %s", state.currentDefaultScene.c_str());
        }

        ImGui::End();
    }

    void InspectorWindow::drawEntityInspector(EditorContext& ctx)
    {
        if (!ctx.services)
        {
            ImGui::TextUnformatted("Services are not available.");
            return;
        }

        auto* worldService = ctx.services->tryGet<vultra::IWorldService>();
        if (!worldService)
        {
            ImGui::TextUnformatted("World service is not available.");
            return;
        }

        auto& world = worldService->world();
        auto& reg   = world.registry();
        auto  e     = findEntityByUUID(world, Selection::lastId());
        if (e == entt::null || !reg.valid(e))
        {
            ImGui::TextUnformatted("Selected entity no longer exists.");
            return;
        }

        ImGui::TextUnformatted("Entity");
        ImGui::Separator();

        if (auto* id = reg.try_get<vultra::IDComponent>(e))
            ImGui::TextWrapped("UUID: %s", id->uuid.toString().c_str());

        auto& name = reg.get_or_emplace<vultra::NameComponent>(e, vultra::NameComponent {"Entity"});
        if (m_NameEditEntity != Selection::lastId())
        {
            m_NameEditEntity = Selection::lastId();
            copyName(m_NameBuffer, name.name);
        }
        if (ImGui::InputText("Name", m_NameBuffer.data(), m_NameBuffer.size()))
            name.name = m_NameBuffer.data();

        if (componentHeader<vultra::TransformComponent>("Transform", world, e))
        {
            auto& tr = reg.get<vultra::TransformComponent>(e);
            if (ImGui::DragFloat3("Position", &tr.position.x, 0.05f))
                tr.dirty = true;

            glm::vec3 eulerDeg = glm::degrees(glm::eulerAngles(tr.rotation));
            if (ImGui::DragFloat3("Rotation", &eulerDeg.x, 0.5f))
            {
                tr.rotation = glm::quat(glm::radians(eulerDeg));
                tr.dirty    = true;
            }

            if (ImGui::DragFloat3("Scale", &tr.scale.x, 0.05f, 0.001f, 1000.0f))
                tr.dirty = true;
        }

        if (componentHeader<vultra::HierarchyComponent>("Hierarchy", world, e))
        {
            const auto* h = reg.try_get<vultra::HierarchyComponent>(e);
            ImGui::Text("Children: %u", h ? h->childCount : 0);
            if (h && h->parent != entt::null)
            {
                if (auto* parentId = reg.try_get<vultra::IDComponent>(h->parent))
                    ImGui::TextWrapped("Parent UUID: %s", parentId->uuid.toString().c_str());
            }
            else
            {
                ImGui::TextUnformatted("Parent: <scene root>");
            }
        }

        if (componentHeader<vultra::MeshComponent>("Mesh", world, e))
        {
            auto& mesh = reg.get<vultra::MeshComponent>(e);
            ImGui::TextWrapped("Mesh UUID: %s", mesh.mesh.toString().c_str());
        }

        if (componentHeader<vultra::GaussianSplatComponent>("Gaussian Splat", world, e))
        {
            auto& splat = reg.get<vultra::GaussianSplatComponent>(e);
            ImGui::TextWrapped("Splat UUID: %s", splat.gaussianSplat.toString().c_str());
        }

        if (componentHeader<vultra::ScriptComponent>("Script", world, e))
        {
            auto& script = reg.get<vultra::ScriptComponent>(e);
            ImGui::Checkbox("Enabled", &script.enabled);
            ImGui::TextWrapped("Script URI: %s", script.scriptUri.c_str());
        }

        if (componentHeader<vultra::PrefabInstanceComponent>("Prefab", world, e))
        {
            auto& prefab = reg.get<vultra::PrefabInstanceComponent>(e);
            ImGui::TextWrapped("URI: %s", prefab.prefabUri.c_str());
            if (prefab.prefabId.valid())
                ImGui::TextWrapped("UUID: %s", prefab.prefabId.toString().c_str());
        }

        if (ImGui::Button("Add Transform") && !reg.all_of<vultra::TransformComponent>(e))
            reg.emplace<vultra::TransformComponent>(e);
        ImGui::SameLine();
        if (ImGui::Button("Delete Entity"))
        {
            world.destroyRecursive(e);
            Selection::clear(SelectionCategory::Entity);
            ctx.state.statusMessage = "Deleted entity.";
        }
    }

    void InspectorWindow::drawAssetInspector(EditorContext& ctx)
    {
        if (!ctx.services)
        {
            ImGui::TextUnformatted("Services are not available.");
            return;
        }

        auto* assetService = ctx.services->tryGet<vultra::IAssetService>();
        if (!assetService)
        {
            ImGui::TextUnformatted("Asset service is not available.");
            return;
        }

        const auto uuid  = Selection::lastId();
        const auto entry = assetService->registry().lookup(uuid.native());

        ImGui::TextUnformatted("Asset");
        ImGui::Separator();
        ImGui::TextWrapped("UUID: %s", uuid.toString().c_str());
        ImGui::TextWrapped("Type: %s", vasset::toString(entry.type).c_str());
        ImGui::TextWrapped("Source: %s", entry.sourcePath.c_str());
        ImGui::TextWrapped("Imported: %s", entry.importedPath.c_str());
    }

    void InspectorWindow::drawSourceAssetInspector(EditorContext& ctx)
    {
        const auto& path = ctx.state.selectedSourceAsset;
        const auto  ext  = path.extension().generic_string();

        ImGui::TextUnformatted("Source Asset");
        ImGui::Separator();
        ImGui::TextWrapped("Name: %s", path.filename().generic_string().c_str());
        ImGui::TextWrapped("Type: %s", ext.empty() ? "Folder" : ext.c_str());
        ImGui::TextWrapped("Path: %s", path.generic_string().c_str());

        std::error_code ec;
        if (std::filesystem::is_regular_file(path, ec))
            ImGui::Text("Size: %llu bytes", static_cast<unsigned long long>(std::filesystem::file_size(path, ec)));

        if (ui::isTextureSourceAsset(path))
        {
            ImGui::Spacing();
            drawSourceTexturePreview(ctx, path);
        }
        else if (sourceAssetHasExtension(path, {".vscn"}))
        {
            ImGui::Spacing();
            ImGui::TextUnformatted("Scene source");
            if (ImGui::Button("Set As Default Scene"))
            {
                const auto assetRoot = ctx.state.currentProject / ctx.state.currentAssetRoot;
                std::error_code relEc;
                auto rel = std::filesystem::relative(path, assetRoot, relEc);
                if (!relEc)
                {
                    ctx.state.currentDefaultScene = "res://" + rel.generic_string();
                    ctx.state.statusMessage       = "Default scene set to: " + ctx.state.currentDefaultScene;
                }
            }
        }
    }

    void InspectorWindow::drawSourceTexturePreview(EditorContext& ctx, const std::filesystem::path& path)
    {
        const auto previewId = m_PreviewCache.getTexturePreview(ctx, path);
        if (!previewId)
        {
            drawImagePreviewPlaceholder(path, m_PreviewCache.lastError().c_str());
            return;
        }

        ImGui::TextUnformatted("Preview");
        const float size = std::min(ImGui::GetContentRegionAvail().x, 260.0f);
        ImGui::Image(previewId, ImVec2(size, size));
    }
} // namespace vultra_app
