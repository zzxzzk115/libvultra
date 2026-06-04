#include "editor_app/ui/windows/world_viewer_window.hpp"

#include "editor_app/editor_context.hpp"

#include <IconsMaterialDesignIcons.h>
#include <vultra/function/services/world_service.hpp>
#include <vultra/function/world/components/animator_component.hpp>
#include <vultra/function/world/components/box_shape_component.hpp>
#include <vultra/function/world/components/camera_component.hpp>
#include <vultra/function/world/components/capsule_shape_component.hpp>
#include <vultra/function/world/components/entity_status_component.hpp>
#include <vultra/function/world/components/environment_component.hpp>
#include <vultra/function/world/components/gaussian_splat_component.hpp>
#include <vultra/function/world/components/hierarchy_component.hpp>
#include <vultra/function/world/components/id_component.hpp>
#include <vultra/function/world/components/layer_component.hpp>
#include <vultra/function/world/components/light_component.hpp>
#include <vultra/function/world/components/mesh_component.hpp>
#include <vultra/function/world/components/name_component.hpp>
#include <vultra/function/world/components/prefab_instance_component.hpp>
#include <vultra/function/world/components/reflection_probe_component.hpp>
#include <vultra/function/world/components/rigid_body_component.hpp>
#include <vultra/function/world/components/script_component.hpp>
#include <vultra/function/world/components/skin_palette_component.hpp>
#include <vultra/function/world/components/sphere_shape_component.hpp>
#include <vultra/function/world/components/transform_component.hpp>
#include <vultra/function/world/components/ui_components.hpp>
#include <vultra/function/world/components/xr_view_component.hpp>
#include <vultra/function/world/world.hpp>

#include <entt/entt.hpp>
#include <imgui.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace vultra_app
{
    namespace
    {
        struct PoolStat
        {
            std::string name;
            std::size_t count {0};
            std::size_t bytes {0};
        };

        struct WorldStats
        {
            std::uint64_t         id {0};
            std::string           name;
            bool                  isActive {false};
            std::size_t           entityCount {0};
            std::vector<PoolStat> pools;
            std::size_t           totalBytes {0};
        };

        // sizeof() of the known engine component types, keyed by their EnTT storage id
        // (entt::type_hash<T>::value()). Used to turn per-pool component counts into an estimated
        // CPU byte footprint. Pools not in the table fall back to index-overhead only, so this is
        // an estimate by design -the real leak signal is the entity / pool counts.
        const std::unordered_map<entt::id_type, std::size_t>& componentSizeTable()
        {
            static const std::unordered_map<entt::id_type, std::size_t> table = [] {
                std::unordered_map<entt::id_type, std::size_t> m;
#define VULTRA_WV_ADD(T) m.emplace(entt::type_hash<vultra::T>::value(), sizeof(vultra::T))
                VULTRA_WV_ADD(AnimatorComponent);
                VULTRA_WV_ADD(BoxShapeComponent);
                VULTRA_WV_ADD(CameraComponent);
                VULTRA_WV_ADD(CapsuleShapeComponent);
                VULTRA_WV_ADD(EntityStatusComponent);
                VULTRA_WV_ADD(EnvironmentComponent);
                VULTRA_WV_ADD(GaussianSplatComponent);
                VULTRA_WV_ADD(HierarchyComponent);
                VULTRA_WV_ADD(IDComponent);
                VULTRA_WV_ADD(LayerComponent);
                VULTRA_WV_ADD(LightComponent);
                VULTRA_WV_ADD(MeshComponent);
                VULTRA_WV_ADD(NameComponent);
                VULTRA_WV_ADD(PrefabInstanceComponent);
                VULTRA_WV_ADD(ReflectionProbeComponent);
                VULTRA_WV_ADD(RigidBodyComponent);
                VULTRA_WV_ADD(ScriptComponent);
                VULTRA_WV_ADD(SkinPaletteComponent);
                VULTRA_WV_ADD(SphereShapeComponent);
                VULTRA_WV_ADD(TransformComponent);
                VULTRA_WV_ADD(XRViewComponent);
                VULTRA_WV_ADD(CanvasComponent);
                VULTRA_WV_ADD(RectTransformComponent);
                VULTRA_WV_ADD(UiButtonComponent);
                VULTRA_WV_ADD(UiImageComponent);
                VULTRA_WV_ADD(UiLayoutComponent);
                VULTRA_WV_ADD(UiPanelComponent);
                VULTRA_WV_ADD(UiProgressBarComponent);
                VULTRA_WV_ADD(UiSliderComponent);
                VULTRA_WV_ADD(UiTextComponent);
                VULTRA_WV_ADD(UiToggleComponent);
#undef VULTRA_WV_ADD
                return m;
            }();
            return table;
        }

        std::string shortTypeName(std::string_view full)
        {
            // EnTT pretty names look like "struct vultra::TransformComponent" -keep the last segment.
            if (const auto pos = full.find_last_of(':'); pos != std::string_view::npos)
                full.remove_prefix(pos + 1);
            return std::string {full};
        }

        std::string humanizeBytes(std::size_t bytes)
        {
            char        buf[32];
            const auto  value = static_cast<double>(bytes);
            if (bytes < 1024)
                std::snprintf(buf, sizeof(buf), "%zu B", bytes);
            else if (bytes < 1024ull * 1024ull)
                std::snprintf(buf, sizeof(buf), "%.1f KB", value / 1024.0);
            else
                std::snprintf(buf, sizeof(buf), "%.2f MB", value / (1024.0 * 1024.0));
            return std::string {buf};
        }

        WorldStats collectWorldStats(vultra::World& world, const vultra::World* activeWorld)
        {
            WorldStats stats;
            stats.id       = world.instanceId();
            stats.name     = world.debugName().empty() ? std::string {"<unnamed>"} : world.debugName();
            stats.isActive = (&world == activeWorld);

            auto& reg = world.registry();
            // free_list() on the entity storage == number of in-use (alive) entities.
            stats.entityCount = static_cast<std::size_t>(reg.storage<entt::entity>().free_list());

            const auto  entityPoolId = entt::type_hash<entt::entity>::value();
            std::size_t total        = 0;
            for (auto&& [id, pool] : reg.storage())
            {
                if (id == entityPoolId)
                    continue;

                const std::size_t count = pool.size();
                // Packed-entity index overhead is always present; add the component payload when known.
                std::size_t elementBytes = sizeof(entt::entity);
                if (const auto it = componentSizeTable().find(id); it != componentSizeTable().end())
                    elementBytes += it->second;

                const std::size_t bytes = count * elementBytes;
                total += bytes;
                stats.pools.push_back(PoolStat {shortTypeName(pool.info().name()), count, bytes});
            }

            std::sort(stats.pools.begin(), stats.pools.end(), [](const PoolStat& a, const PoolStat& b) {
                return a.bytes > b.bytes;
            });
            stats.totalBytes = total;
            return stats;
        }
    } // namespace

    WorldViewerWindow::WorldViewerWindow() : EditorWindow("World Viewer", ICON_MDI_EARTH)
    {
        // Debug/diagnostic tool -opened on demand from the Tools menu, hidden by default.
        m_Open = false;
    }

    void WorldViewerWindow::draw(EditorContext& ctx)
    {
        if (!m_Open)
            return;

        if (!ImGui::Begin(title().c_str(), &m_Open))
        {
            ImGui::End();
            return;
        }

        const vultra::World* activeWorld = nullptr;
        if (ctx.services)
        {
            if (auto* worldService = ctx.services->tryGet<vultra::IWorldService>())
                activeWorld = &worldService->world();
        }

        std::vector<WorldStats> worlds;
        vultra::World::forEachLive([&](vultra::World& world) { worlds.push_back(collectWorldStats(world, activeWorld)); });
        std::sort(worlds.begin(), worlds.end(), [](const WorldStats& a, const WorldStats& b) { return a.id < b.id; });

        std::size_t totalEntities = 0;
        std::size_t totalBytes    = 0;
        for (const auto& w : worlds)
        {
            totalEntities += w.entityCount;
            totalBytes += w.totalBytes;
        }

        ImGui::Text("Live worlds: %zu", worlds.size());
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        ImGui::Text("Entities: %zu", totalEntities);
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        ImGui::Text("Est. CPU: %s", humanizeBytes(totalBytes).c_str());

        ImGui::Checkbox("Expand component pools", &m_ExpandPools);
        ImGui::TextDisabled("Worlds beyond the active one usually indicate previews or unreleased staging worlds.");
        ImGui::Separator();

        for (const auto& w : worlds)
        {
            ImGui::PushID(static_cast<int>(w.id));

            char header[256];
            std::snprintf(header,
                          sizeof(header),
                          "%s  #%llu%s  -  %zu entities, %zu pools, %s",
                          w.name.c_str(),
                          static_cast<unsigned long long>(w.id),
                          w.isActive ? " (active)" : "",
                          w.entityCount,
                          w.pools.size(),
                          humanizeBytes(w.totalBytes).c_str());

            // The "Expand component pools" checkbox gates expansion: when off, the header is a
            // non-collapsible leaf (no arrow, can't open); when on, it is a normal default-open
            // header the user can still collapse individually.
            const ImGuiTreeNodeFlags headerFlags =
                m_ExpandPools ? ImGuiTreeNodeFlags_DefaultOpen
                              : (ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
            if (ImGui::CollapsingHeader(header, headerFlags) && m_ExpandPools)
            {
                if (w.pools.empty())
                {
                    ImGui::TextDisabled("  (no components)");
                }
                else if (ImGui::BeginTable("pools", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH))
                {
                    ImGui::TableSetupColumn("Component", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed);
                    ImGui::TableSetupColumn("Est. CPU", ImGuiTableColumnFlags_WidthFixed);
                    ImGui::TableHeadersRow();
                    for (const auto& pool : w.pools)
                    {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(pool.name.c_str());
                        ImGui::TableNextColumn();
                        ImGui::Text("%zu", pool.count);
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(humanizeBytes(pool.bytes).c_str());
                    }
                    ImGui::EndTable();
                }
            }

            ImGui::PopID();
        }

        ImGui::End();
    }
} // namespace vultra_app
