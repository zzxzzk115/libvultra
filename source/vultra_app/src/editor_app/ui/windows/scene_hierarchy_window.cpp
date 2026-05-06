#include "editor_app/ui/windows/scene_hierarchy_window.hpp"

#include "editor_app/selection.hpp"

#include <vultra/function/services/world_service.hpp>
#include <vultra/function/world/components/id_component.hpp>
#include <vultra/function/world/components/name_component.hpp>
#include <vultra/function/world/components/transform_component.hpp>
#include <vultra/function/world/world.hpp>

#include <imgui.h>

namespace vultra_app
{
    SceneHierarchyWindow::SceneHierarchyWindow() : EditorWindow("Scene Hierarchy") {}

    void SceneHierarchyWindow::draw(EditorContext& ctx)
    {
        ImGui::Begin(m_Name.c_str(), &m_Open);
        ImGui::TextUnformatted(ctx.state.currentDefaultScene.c_str());
        ImGui::SameLine();
        if (ImGui::Button("Create Empty") && ctx.services)
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

        bool drewAny = false;
        auto view    = reg.view<vultra::IDComponent>();
        for (auto entity : view)
        {
            if (world.parent(entity) != entt::null)
                continue;

            drewAny = true;
            drawEntityNode(ctx, world, entity);
        }

        if (!drewAny)
            ImGui::TextDisabled("No entities yet.");

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

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (world.firstChild(entity) == entt::null)
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        if (Selection::isSelected(SelectionCategory::Entity, id->uuid))
            flags |= ImGuiTreeNodeFlags_Selected;

        ImGui::PushID(static_cast<int>(entt::to_integral(entity)));
        const bool opened = ImGui::TreeNodeEx("##entity", flags, "%s", name.c_str());
        if (ImGui::IsItemClicked())
        {
            ctx.state.selectedSourceAsset.clear();
            Selection::select(SelectionCategory::Entity, id->uuid);
        }

        if (ImGui::BeginPopupContextItem("EntityContext"))
        {
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

        if (opened && !(flags & ImGuiTreeNodeFlags_NoTreePushOnOpen))
        {
            for (auto child = world.firstChild(entity); child != entt::null; child = world.nextSibling(child))
                drawEntityNode(ctx, world, child);
            ImGui::TreePop();
        }

        ImGui::PopID();
    }
} // namespace vultra_app
