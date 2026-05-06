#include "editor_app/ui/windows/scene_hierarchy_window.hpp"

#include "common/ui_widgets.hpp"
#include "editor_app/selection.hpp"

#include <IconsMaterialDesignIcons.h>
#include <vultra/function/services/world_service.hpp>
#include <vultra/function/world/components/camera_component.hpp>
#include <vultra/function/world/components/entity_status_component.hpp>
#include <vultra/function/world/components/gaussian_splat_component.hpp>
#include <vultra/function/world/components/id_component.hpp>
#include <vultra/function/world/components/mesh_component.hpp>
#include <vultra/function/world/components/name_component.hpp>
#include <vultra/function/world/components/transform_component.hpp>
#include <vultra/function/world/world.hpp>

#include <imgui.h>

#include <cstring>
#include <string>

namespace vultra_app
{
    namespace
    {
        bool isDescendantOf(vultra::World& world, entt::entity entity, entt::entity possibleAncestor)
        {
            for (auto parent = world.parent(entity); parent != entt::null; parent = world.parent(parent))
            {
                if (parent == possibleAncestor)
                    return true;
            }
            return false;
        }

        const char* entityIcon(vultra::World& world, entt::entity entity)
        {
            auto& reg = world.registry();
            if (reg.all_of<vultra::CameraComponent>(entity))
                return ICON_MDI_CAMERA;
            if (reg.all_of<vultra::MeshComponent>(entity))
                return ICON_MDI_CUBE;
            if (reg.all_of<vultra::GaussianSplatComponent>(entity))
                return ICON_MDI_VECTOR_POINT;
            return ICON_MDI_CUBE_OUTLINE;
        }
    } // namespace

    SceneHierarchyWindow::SceneHierarchyWindow() : EditorWindow("Scene Hierarchy") {}

    void SceneHierarchyWindow::draw(EditorContext& ctx)
    {
        ImGui::Begin(m_Name.c_str(), &m_Open);
        ImGui::TextColored(ImVec4(0.72f, 0.80f, 0.92f, 1.0f), "%s", ICON_MDI_FILE_TREE);
        ImGui::SameLine();
        ImGui::TextUnformatted(ctx.state.currentDefaultScene.c_str());
        ImGui::SameLine();
        if (ImGui::Button(ICON_MDI_PLUS "  Create Empty") && ctx.services)
        {
            if (auto* worldService = ctx.services->tryGet<vultra::IWorldService>())
            {
                auto& world = worldService->world();
                auto& reg   = world.registry();
                auto  e     = world.createEntity();
                reg.emplace<vultra::NameComponent>(e, vultra::NameComponent {"Empty Entity"});
                reg.emplace<vultra::TransformComponent>(e);
                if (auto* id = reg.try_get<vultra::IDComponent>(e))
                    Selection::select(SelectionCategory::Entity, id->uuid);
                ctx.state.statusMessage = "Created empty entity.";
            }
        }
        ImGui::Separator();

        if (!ctx.services)
        {
            ImGui::TextUnformatted("Services are not available.");
            ImGui::End();
            return;
        }

        auto* worldService = ctx.services->tryGet<vultra::IWorldService>();
        if (!worldService)
        {
            ImGui::TextUnformatted("World service is not available.");
            ImGui::End();
            return;
        }

        auto& world = worldService->world();
        auto& reg   = world.registry();

        if (ImGui::BeginPopupContextWindow("SceneHierarchyContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
        {
            if (ImGui::MenuItem("Create Empty Entity"))
            {
                auto e = world.createEntity();
                reg.emplace<vultra::NameComponent>(e, vultra::NameComponent {"Empty Entity"});
                reg.emplace<vultra::TransformComponent>(e);
                if (auto* id = reg.try_get<vultra::IDComponent>(e))
                    Selection::select(SelectionCategory::Entity, id->uuid);
            }
            ImGui::EndPopup();
        }

        if (ImGui::BeginTable("SceneHierarchyTable",
                              2,
                              ImGuiTableFlags_Resizable | ImGuiTableFlags_NoPadOuterX |
                                  ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg))
        {
            ImGui::TableSetupColumn("Entity", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 86.0f);
            ImGui::TableHeadersRow();

            bool drewAny = false;
            auto view    = reg.view<vultra::IDComponent>();
            for (auto entity : view)
            {
                if (world.parent(entity) != entt::null)
                    continue;

                drewAny = true;
                drawEntityNode(ctx, world, entity);
            }

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::InvisibleButton("scene_empty_space",
                                   ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y));
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("VULTRA_ENTITY"))
                {
                    if (payload->DataSize == sizeof(entt::entity))
                    {
                        entt::entity dropped {};
                        std::memcpy(&dropped, payload->Data, sizeof(entt::entity));
                        if (reg.valid(dropped))
                        {
                            if (auto* status = reg.try_get<vultra::EntityStatusComponent>(dropped); status && status->locked)
                                ctx.state.statusMessage = "Entity is locked.";
                            else
                            {
                                world.removeParent(dropped);
                                ctx.state.statusMessage = "Moved entity to scene root.";
                            }
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }

            if (!drewAny)
                ui::emptyState(ICON_MDI_CUBE_OFF_OUTLINE, "No Entities", "Right-click or use Create Empty to add one.");

            ImGui::EndTable();
        }

        ImGui::End();
    }

    void SceneHierarchyWindow::drawEntityNode(EditorContext& ctx, vultra::World& world, entt::entity entity)
    {
        auto& reg = world.registry();
        if (!reg.valid(entity))
            return;

        auto* id = reg.try_get<vultra::IDComponent>(entity);
        if (!id)
            return;

        auto*       nameComponent = reg.try_get<vultra::NameComponent>(entity);
        std::string name          = nameComponent ? nameComponent->name : "Entity";
        const auto  label         = std::string(entityIcon(world, entity)) + "  " + name;
        auto&       status        = reg.get_or_emplace<vultra::EntityStatusComponent>(entity);

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (world.firstChild(entity) == entt::null)
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        if (Selection::isSelected(SelectionCategory::Entity, id->uuid))
            flags |= ImGuiTreeNodeFlags_Selected;

        ImGui::PushID(static_cast<int>(entt::to_integral(entity)));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        const bool opened = ImGui::TreeNodeEx("##entity", flags, "%s", label.c_str());
        if (ImGui::IsItemClicked() && status.selectable)
        {
            ctx.state.selectedSourceAsset.clear();
            Selection::select(SelectionCategory::Entity, id->uuid);
        }
        if (ImGui::IsItemHovered() && ImGui::IsKeyPressed(ImGuiKey_F2))
        {
            m_RenameEntity = entity;
            std::memset(m_RenameBuffer.data(), 0, m_RenameBuffer.size());
            std::memcpy(m_RenameBuffer.data(), name.c_str(), std::min(name.size(), m_RenameBuffer.size() - 1));
            ImGui::OpenPopup("Rename Entity");
        }

        if (!status.locked && ImGui::BeginDragDropSource())
        {
            ImGui::SetDragDropPayload("VULTRA_ENTITY", &entity, sizeof(entity));
            ImGui::TextUnformatted(name.c_str());
            ImGui::EndDragDropSource();
        }

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("VULTRA_ENTITY"))
            {
                if (payload->DataSize == sizeof(entt::entity))
                {
                    entt::entity dropped {};
                    std::memcpy(&dropped, payload->Data, sizeof(entt::entity));
                    if (reg.valid(dropped) && dropped != entity && !isDescendantOf(world, entity, dropped))
                    {
                        if (auto* draggedStatus = reg.try_get<vultra::EntityStatusComponent>(dropped);
                            draggedStatus && draggedStatus->locked)
                            ctx.state.statusMessage = "Entity is locked.";
                        else
                        {
                            world.setParent(dropped, entity);
                            ctx.state.statusMessage = "Reparented entity.";
                        }
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }

        if (ImGui::BeginPopupContextItem("EntityContext"))
        {
            if (ImGui::MenuItem("Rename"))
            {
                m_RenameEntity = entity;
                std::memset(m_RenameBuffer.data(), 0, m_RenameBuffer.size());
                std::memcpy(m_RenameBuffer.data(), name.c_str(), std::min(name.size(), m_RenameBuffer.size() - 1));
                ImGui::OpenPopup("Rename Entity");
            }
            if (ImGui::MenuItem("Create Child"))
            {
                auto child = world.createChild(entity);
                reg.emplace<vultra::NameComponent>(child, vultra::NameComponent {"Child Entity"});
                reg.emplace<vultra::TransformComponent>(child);
                if (auto* childId = reg.try_get<vultra::IDComponent>(child))
                    Selection::select(SelectionCategory::Entity, childId->uuid);
            }
            if (ImGui::MenuItem("Delete"))
            {
                world.destroyRecursive(entity);
                Selection::clear(SelectionCategory::Entity);
                ctx.state.statusMessage = "Deleted entity.";
                ImGui::EndPopup();
                ImGui::PopID();
                return;
            }
            ImGui::EndPopup();
        }

        ImGui::TableNextColumn();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 6.0f);
        if (ui::iconButton(status.visible ? ICON_MDI_EYE : ICON_MDI_EYE_OFF, "Toggle visibility", status.visible))
            status.visible = !status.visible;
        ImGui::SameLine();
        if (ui::iconButton(status.locked ? ICON_MDI_LOCK : ICON_MDI_LOCK_OPEN_VARIANT, "Toggle lock", status.locked))
            status.locked = !status.locked;

        if (m_RenameEntity == entity && ImGui::BeginPopupModal("Rename Entity", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::InputText("Name", m_RenameBuffer.data(), m_RenameBuffer.size());
            if (ImGui::Button("OK"))
            {
                reg.get_or_emplace<vultra::NameComponent>(entity).name = m_RenameBuffer.data();
                m_RenameEntity = entt::null;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                m_RenameEntity = entt::null;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (opened && !(flags & ImGuiTreeNodeFlags_NoTreePushOnOpen))
        {
            for (auto child = world.firstChild(entity); child != entt::null; child = world.nextSibling(child))
                drawEntityNode(ctx, world, child);
            ImGui::TreePop();
        }

        ImGui::PopID();
    }
} // namespace vultra_app
