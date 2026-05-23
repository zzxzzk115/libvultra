#include "editor_app/ui/windows/scene_hierarchy_window.hpp"

#include "common/ui_widgets.hpp"
#include "editor_app/selection.hpp"

#include <IconsMaterialDesignIcons.h>
#include <vultra/function/services/world_service.hpp>
#include <vultra/function/world/components/camera_component.hpp>
#include <vultra/function/world/components/entity_status_component.hpp>
#include <vultra/function/world/components/gaussian_splat_component.hpp>
#include <vultra/function/world/components/id_component.hpp>
#include <vultra/function/world/components/light_component.hpp>
#include <vultra/function/world/components/mesh_component.hpp>
#include <vultra/function/world/components/name_component.hpp>
#include <vultra/function/world/components/transform_component.hpp>
#include <vultra/function/world/world.hpp>

#include <imgui.h>

#include <cstring>
#include <algorithm>
#include <cctype>
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
            if (reg.all_of<vultra::LightComponent>(entity))
                return ICON_MDI_LIGHTBULB_ON_OUTLINE;
            if (reg.all_of<vultra::MeshComponent>(entity))
                return ICON_MDI_CUBE;
            if (reg.all_of<vultra::GaussianSplatComponent>(entity))
                return ICON_MDI_VECTOR_POINT;
            return ICON_MDI_CUBE_OUTLINE;
        }

        std::string lowercase(std::string text)
        {
            std::transform(text.begin(),
                           text.end(),
                           text.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            return text;
        }

        enum class EntityDropMode
        {
            Before,
            AsChild,
            After,
        };

        EntityDropMode dropModeForItem(const ImVec2& itemMin, const ImVec2& itemMax)
        {
            const float height = itemMax.y - itemMin.y;
            if (height <= 0.0f)
                return EntityDropMode::AsChild;

            const float y = ImGui::GetMousePos().y;
            if (y < itemMin.y + height * 0.35f)
                return EntityDropMode::Before;
            if (y > itemMax.y - height * 0.35f)
                return EntityDropMode::After;
            return EntityDropMode::AsChild;
        }

        void drawDropIndicator(const ImVec2& itemMin, const ImVec2& itemMax, EntityDropMode mode)
        {
            auto* drawList = ImGui::GetWindowDrawList();
            if (!drawList)
                return;

            const ImU32 accent = ImGui::GetColorU32(ImGuiCol_DragDropTarget);
            if (mode == EntityDropMode::AsChild)
            {
                drawList->AddRect(itemMin, itemMax, accent, 3.0f, 0, 2.0f);
                return;
            }

            const float y = mode == EntityDropMode::Before ? itemMin.y : itemMax.y;
            const float x0 = itemMin.x + 2.0f;
            const float x1 = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x - 2.0f;
            drawList->AddLine(ImVec2(x0, y), ImVec2(x1, y), accent, 2.0f);
            drawList->AddCircleFilled(ImVec2(x0, y), 3.0f, accent);
        }
    } // namespace

    SceneHierarchyWindow::SceneHierarchyWindow() : EditorWindow("Scene Hierarchy", ICON_MDI_FILE_TREE) {}

    void SceneHierarchyWindow::draw(EditorContext& ctx)
    {
        ImGuiWindowFlags windowFlags = 0;
        if (ctx.state.sceneDirty)
            windowFlags |= ImGuiWindowFlags_UnsavedDocument;
        ImGui::Begin(title().c_str(), &m_Open, windowFlags);
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
                static_cast<void>(reg.get_or_emplace<vultra::TransformComponent>(e));
                if (auto* id = reg.try_get<vultra::IDComponent>(e))
                    Selection::select(SelectionCategory::Entity, id->uuid);
                ctx.state.sceneDirty = true;
                ctx.state.statusMessage = "Created empty entity.";
            }
        }
        ImGui::Separator();

        ImGui::TextUnformatted(ICON_MDI_MAGNIFY);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##SceneHierarchySearch", "Search entities...", m_SearchBuffer.data(), m_SearchBuffer.size());

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
                static_cast<void>(reg.get_or_emplace<vultra::TransformComponent>(e));
                if (auto* id = reg.try_get<vultra::IDComponent>(e))
                    Selection::select(SelectionCategory::Entity, id->uuid);
                ctx.state.sceneDirty = true;
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
            for (auto entity = world.firstChild(entt::null); entity != entt::null; entity = world.nextSibling(entity))
            {
                if (!entityMatchesFilter(world, entity, m_SearchBuffer.data()))
                    continue;

                drewAny = true;
                drawEntityNode(ctx, world, entity, m_SearchBuffer.data());
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
                                ctx.state.sceneDirty = true;
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

    bool SceneHierarchyWindow::entityMatchesFilter(vultra::World& world, entt::entity entity, const char* filter) const
    {
        if (filter == nullptr || filter[0] == '\0')
            return true;

        auto& reg = world.registry();
        if (!reg.valid(entity))
            return false;

        const auto needle = lowercase(filter);
        const auto* nameComponent = reg.try_get<vultra::NameComponent>(entity);
        const auto name = lowercase(nameComponent ? nameComponent->name : "Entity");
        if (name.find(needle) != std::string::npos)
            return true;

        for (auto child = world.firstChild(entity); child != entt::null; child = world.nextSibling(child))
        {
            if (entityMatchesFilter(world, child, filter))
                return true;
        }
        return false;
    }

    void SceneHierarchyWindow::drawEntityNode(EditorContext& ctx, vultra::World& world, entt::entity entity, const char* filter)
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
        if (world.parent(entity) == entt::null || (filter != nullptr && filter[0] != '\0'))
            flags |= ImGuiTreeNodeFlags_DefaultOpen;

        ImGui::PushID(static_cast<int>(entt::to_integral(entity)));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        const bool opened = ImGui::TreeNodeEx("##entity", flags, "%s", label.c_str());
        const ImVec2 itemMin = ImGui::GetItemRectMin();
        const ImVec2 itemMax = ImGui::GetItemRectMax();
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
            const EntityDropMode dropMode = dropModeForItem(itemMin, itemMax);
            drawDropIndicator(itemMin, itemMax, dropMode);

            if (const ImGuiPayload* payload =
                    ImGui::AcceptDragDropPayload("VULTRA_ENTITY", ImGuiDragDropFlags_AcceptNoDrawDefaultRect))
            {
                if (payload->DataSize == sizeof(entt::entity))
                {
                    entt::entity dropped {};
                    std::memcpy(&dropped, payload->Data, sizeof(entt::entity));
                    const bool           validChildDrop =
                        dropMode == EntityDropMode::AsChild && !isDescendantOf(world, entity, dropped);
                    const entt::entity targetParent = world.parent(entity);
                    const bool         validSiblingDrop =
                        dropMode != EntityDropMode::AsChild && targetParent != dropped &&
                        (targetParent == entt::null || !isDescendantOf(world, targetParent, dropped));

                    if (reg.valid(dropped) && dropped != entity && (validChildDrop || validSiblingDrop))
                    {
                        if (auto* draggedStatus = reg.try_get<vultra::EntityStatusComponent>(dropped);
                            draggedStatus && draggedStatus->locked)
                            ctx.state.statusMessage = "Entity is locked.";
                        else
                        {
                            if (dropMode == EntityDropMode::Before)
                            {
                                world.insertBefore(dropped, entity);
                                ctx.state.statusMessage = "Moved entity above sibling.";
                            }
                            else if (dropMode == EntityDropMode::After)
                            {
                                world.insertAfter(dropped, entity);
                                ctx.state.statusMessage = "Moved entity below sibling.";
                            }
                            else
                            {
                                world.setParent(dropped, entity);
                                ctx.state.statusMessage = "Reparented entity.";
                            }
                            ctx.state.sceneDirty = true;
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
                static_cast<void>(reg.get_or_emplace<vultra::TransformComponent>(child));
                if (auto* childId = reg.try_get<vultra::IDComponent>(child))
                    Selection::select(SelectionCategory::Entity, childId->uuid);
                ctx.state.sceneDirty = true;
            }
            if (ImGui::MenuItem("Delete"))
            {
                world.destroyRecursive(entity);
                Selection::clear(SelectionCategory::Entity);
                ctx.state.sceneDirty = true;
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
        {
            status.visible = !status.visible;
            ctx.state.sceneDirty = true;
        }
        ImGui::SameLine();
        if (ui::iconButton(status.locked ? ICON_MDI_LOCK : ICON_MDI_LOCK_OPEN_VARIANT, "Toggle lock", status.locked))
        {
            status.locked = !status.locked;
            ctx.state.sceneDirty = true;
        }

        if (m_RenameEntity == entity && ImGui::BeginPopupModal("Rename Entity", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::InputText("Name", m_RenameBuffer.data(), m_RenameBuffer.size());
            if (ImGui::Button("OK"))
            {
                reg.get_or_emplace<vultra::NameComponent>(entity).name = m_RenameBuffer.data();
                m_RenameEntity = entt::null;
                ctx.state.sceneDirty = true;
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
            {
                if (entityMatchesFilter(world, child, filter))
                    drawEntityNode(ctx, world, child, filter);
            }
            ImGui::TreePop();
        }

        ImGui::PopID();
    }
} // namespace vultra_app
