#include "editor_app/ui/windows/scene_hierarchy_window.hpp"

#include "common/ui_widgets.hpp"
#include "editor_app/editor_app.hpp"
#include "editor_app/editor_history.hpp"
#include "editor_app/scene_asset_instantiation.hpp"
#include "editor_app/selection.hpp"

#include <IconsMaterialDesignIcons.h>
#include <vultra/function/services/asset_service.hpp>
#include <vultra/function/services/scene_service.hpp>
#include <vultra/function/services/world_service.hpp>
#include <vultra/function/world/components/box_shape_component.hpp>
#include <vultra/function/world/components/camera_component.hpp>
#include <vultra/function/world/components/capsule_shape_component.hpp>
#include <vultra/function/world/components/entity_status_component.hpp>
#include <vultra/function/world/components/environment_component.hpp>
#include <vultra/function/world/components/gaussian_splat_component.hpp>
#include <vultra/function/world/components/id_component.hpp>
#include <vultra/function/world/components/light_component.hpp>
#include <vultra/function/world/components/mesh_component.hpp>
#include <vultra/function/world/components/name_component.hpp>
#include <vultra/function/world/components/reflection_probe_component.hpp>
#include <vultra/function/world/components/rigid_body_component.hpp>
#include <vultra/function/world/components/sphere_shape_component.hpp>
#include <vultra/function/world/components/transform_component.hpp>
#include <vultra/function/world/components/ui_components.hpp>
#include <vultra/function/world/components/xr_view_component.hpp>
#include <vultra/function/world/world.hpp>

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>

namespace vultra_app
{
    namespace
    {
        constexpr const char* kAssetUuidPayload = "VULTRA_ASSET_UUID";

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
            if (reg.all_of<vultra::CameraComponent, vultra::XRViewComponent>(entity))
                return ICON_MDI_VIRTUAL_REALITY;
            if (reg.all_of<vultra::CameraComponent>(entity))
                return ICON_MDI_CAMERA;
            if (reg.all_of<vultra::EnvironmentComponent>(entity))
                return ICON_MDI_WEATHER_SUNNY;
            if (reg.all_of<vultra::ReflectionProbeComponent>(entity))
                return ICON_MDI_CUBE_SCAN;
            if (reg.all_of<vultra::LightComponent>(entity))
                return ICON_MDI_LIGHTBULB_ON_OUTLINE;
            if (reg.all_of<vultra::RigidBodyComponent>(entity))
                return ICON_MDI_ATOM;
            if (reg.all_of<vultra::MeshComponent>(entity))
                return ICON_MDI_CUBE;
            if (reg.all_of<vultra::GaussianSplatComponent>(entity))
                return ICON_MDI_VECTOR_POINT;
            if (reg.all_of<vultra::CanvasComponent>(entity))
                return ICON_MDI_MONITOR;
            if (reg.all_of<vultra::RectTransformComponent>(entity))
                return ICON_MDI_APPLICATION;
            return ICON_MDI_CUBE_OUTLINE;
        }

        std::string lowercase(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            return text;
        }

        enum class EntityDropMode
        {
            Before,
            AsChild,
            After,
        };

        enum class SceneCreateKind
        {
            Empty,
            Quad,
            Cube,
            Sphere,
            Capsule,
            DirectionalLight,
            PointLight,
            SpotLight,
            AreaLight,
            Camera,
            XRCamera,
            Environment,
            UiCanvas,
            UiPanel,
            UiText,
            UiImage,
            UiButton,
            UiToggle,
            UiSlider,
            UiProgressBar,
            StaticBox,
            DynamicSphere,
            CapsuleRigidBody,
        };

        const char* commandKindName(const SceneCreateKind kind)
        {
            switch (kind)
            {
                case SceneCreateKind::Empty:
                    return "empty";
                case SceneCreateKind::Quad:
                    return "quad";
                case SceneCreateKind::Cube:
                    return "cube";
                case SceneCreateKind::Sphere:
                    return "sphere";
                case SceneCreateKind::Capsule:
                    return "capsule";
                case SceneCreateKind::DirectionalLight:
                    return "directional_light";
                case SceneCreateKind::PointLight:
                    return "point_light";
                case SceneCreateKind::SpotLight:
                    return "spot_light";
                case SceneCreateKind::AreaLight:
                    return "area_light";
                case SceneCreateKind::Camera:
                    return "camera";
                case SceneCreateKind::XRCamera:
                    return "xr_camera";
                case SceneCreateKind::Environment:
                    return "environment";
                case SceneCreateKind::UiCanvas:
                    return "ui_canvas";
                case SceneCreateKind::UiPanel:
                    return "ui_panel";
                case SceneCreateKind::UiText:
                    return "ui_text";
                case SceneCreateKind::UiImage:
                    return "ui_image";
                case SceneCreateKind::UiButton:
                    return "ui_button";
                case SceneCreateKind::UiToggle:
                    return "ui_toggle";
                case SceneCreateKind::UiSlider:
                    return "ui_slider";
                case SceneCreateKind::UiProgressBar:
                    return "ui_progress_bar";
                case SceneCreateKind::StaticBox:
                    return "static_box";
                case SceneCreateKind::DynamicSphere:
                    return "dynamic_sphere";
                case SceneCreateKind::CapsuleRigidBody:
                    return "capsule_rigidbody";
            }
            return "empty";
        }

        nlohmann::json executeSceneHierarchyCommand(EditorContext& ctx,
                                                    const std::string_view name,
                                                    nlohmann::json         args = nlohmann::json::object())
        {
            if (!ctx.editor)
                return {{"ok", false}, {"error", "editor command executor is unavailable"}};
            auto result = ctx.editor->executeCommand(ctx, name, args);
            if (!result.value("ok", false))
                ctx.state.statusMessage = result.value("error", "editor command failed");
            return result;
        }

        void createSceneEntityCommand(EditorContext& ctx, const entt::entity parent, const SceneCreateKind kind)
        {
            nlohmann::json args {{"entity_kind", commandKindName(kind)}};
            if (parent != entt::null)
                args["parent"] = static_cast<uint32_t>(parent);
            (void)executeSceneHierarchyCommand(ctx, "scene.add_entity", std::move(args));
        }

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

            const float y  = mode == EntityDropMode::Before ? itemMin.y : itemMax.y;
            const float x0 = itemMin.x + 2.0f;
            const float x1 = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x - 2.0f;
            drawList->AddLine(ImVec2(x0, y), ImVec2(x1, y), accent, 2.0f);
            drawList->AddCircleFilled(ImVec2(x0, y), 3.0f, accent);
        }

        bool isDraggingPayload(const char* type)
        {
            const ImGuiPayload* payload = ImGui::GetDragDropPayload();
            return payload && payload->IsDataType(type);
        }

        bool hasPrimaryCamera(vultra::World& world)
        {
            auto& reg  = world.registry();
            auto  view = reg.view<vultra::CameraComponent>();
            for (auto entity : view)
            {
                if (view.get<vultra::CameraComponent>(entity).primary)
                    return true;
            }
            return false;
        }

        void drawCreateEntityMenu(EditorContext& ctx, vultra::World& world, entt::entity parent)
        {
            if (ImGui::MenuItem(ICON_MDI_CUBE_OUTLINE " Empty Entity"))
                createSceneEntityCommand(ctx, parent, SceneCreateKind::Empty);

            if (ImGui::BeginMenu(ICON_MDI_SHAPE " Basic Geometry"))
            {
                if (ImGui::MenuItem(ICON_MDI_VECTOR_SQUARE " Quad"))
                    createSceneEntityCommand(ctx, parent, SceneCreateKind::Quad);
                if (ImGui::MenuItem(ICON_MDI_CUBE " Cube"))
                    createSceneEntityCommand(ctx, parent, SceneCreateKind::Cube);
                if (ImGui::MenuItem(ICON_MDI_SPHERE " Sphere"))
                    createSceneEntityCommand(ctx, parent, SceneCreateKind::Sphere);
                if (ImGui::MenuItem(ICON_MDI_CYLINDER " Capsule"))
                    createSceneEntityCommand(ctx, parent, SceneCreateKind::Capsule);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu(ICON_MDI_LIGHTBULB_ON_OUTLINE " Light"))
            {
                if (ImGui::MenuItem("Directional Light"))
                    createSceneEntityCommand(ctx, parent, SceneCreateKind::DirectionalLight);
                if (ImGui::MenuItem("Point Light"))
                    createSceneEntityCommand(ctx, parent, SceneCreateKind::PointLight);
                if (ImGui::MenuItem("Spot Light"))
                    createSceneEntityCommand(ctx, parent, SceneCreateKind::SpotLight);
                if (ImGui::MenuItem("Area Light"))
                    createSceneEntityCommand(ctx, parent, SceneCreateKind::AreaLight);
                ImGui::EndMenu();
            }

            if (ImGui::MenuItem(ICON_MDI_CAMERA " Camera"))
                createSceneEntityCommand(ctx, parent, SceneCreateKind::Camera);
            if (ImGui::MenuItem(ICON_MDI_VIRTUAL_REALITY " XR Camera"))
                createSceneEntityCommand(ctx, parent, SceneCreateKind::XRCamera);
            if (ImGui::MenuItem(ICON_MDI_WEATHER_SUNNY " Environment"))
                createSceneEntityCommand(ctx, parent, SceneCreateKind::Environment);

            if (ImGui::BeginMenu(ICON_MDI_APPLICATION " UI"))
            {
                if (ImGui::MenuItem(ICON_MDI_MONITOR " Canvas"))
                    createSceneEntityCommand(ctx, parent, SceneCreateKind::UiCanvas);
                if (ImGui::MenuItem(ICON_MDI_RECTANGLE_OUTLINE " Panel"))
                    createSceneEntityCommand(ctx, parent, SceneCreateKind::UiPanel);
                if (ImGui::MenuItem(ICON_MDI_FORMAT_TEXT " Text"))
                    createSceneEntityCommand(ctx, parent, SceneCreateKind::UiText);
                if (ImGui::MenuItem(ICON_MDI_IMAGE_OUTLINE " Image"))
                    createSceneEntityCommand(ctx, parent, SceneCreateKind::UiImage);
                if (ImGui::MenuItem(ICON_MDI_GESTURE_TAP_BUTTON " Button"))
                    createSceneEntityCommand(ctx, parent, SceneCreateKind::UiButton);
                if (ImGui::MenuItem(ICON_MDI_CHECKBOX_MARKED_OUTLINE " Toggle"))
                    createSceneEntityCommand(ctx, parent, SceneCreateKind::UiToggle);
                if (ImGui::MenuItem(ICON_MDI_TUNE " Slider"))
                    createSceneEntityCommand(ctx, parent, SceneCreateKind::UiSlider);
                if (ImGui::MenuItem(ICON_MDI_PROGRESS_CHECK " Progress Bar"))
                    createSceneEntityCommand(ctx, parent, SceneCreateKind::UiProgressBar);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu(ICON_MDI_ATOM " Physics"))
            {
                if (ImGui::MenuItem(ICON_MDI_CUBE " Static Box"))
                    createSceneEntityCommand(ctx, parent, SceneCreateKind::StaticBox);
                if (ImGui::MenuItem(ICON_MDI_SPHERE " Dynamic Sphere"))
                    createSceneEntityCommand(ctx, parent, SceneCreateKind::DynamicSphere);
                if (ImGui::MenuItem(ICON_MDI_CYLINDER " Capsule Rigid Body"))
                    createSceneEntityCommand(ctx, parent, SceneCreateKind::CapsuleRigidBody);
                ImGui::EndMenu();
            }
        }
    } // namespace

    bool SceneHierarchyWindow::acceptAssetDrop(EditorContext& ctx,
                                               vultra::World& world,
                                               entt::entity   parent,
                                               entt::entity   beforeSibling,
                                               entt::entity   afterSibling)
    {
        const ImGuiPayload* payload =
            ImGui::AcceptDragDropPayload(kAssetUuidPayload, ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
        if (!payload || payload->DataSize != sizeof(vultra::CoreUUID))
            return false;

        vultra::CoreUUID uuid;
        std::memcpy(&uuid, payload->Data, sizeof(uuid));

        if (shouldPromptMeshSubAssetPlacement(ctx, uuid))
        {
            m_PendingAssetInstantiation = PendingAssetInstantiation {
                .uuid          = uuid,
                .parent        = parent,
                .beforeSibling = beforeSibling,
                .afterSibling  = afterSibling,
                .keepPosition  = false,
                .keepRotation  = false,
                .keepScale     = true,
                .openPopup     = true,
            };
            return true;
        }

        PendingAssetInstantiation request {
            .uuid          = uuid,
            .parent        = parent,
            .beforeSibling = beforeSibling,
            .afterSibling  = afterSibling,
        };
        return completeAssetInstantiation(ctx, world, request);
    }

    bool SceneHierarchyWindow::completeAssetInstantiation(EditorContext&                   ctx,
                                                          vultra::World&                   world,
                                                          const PendingAssetInstantiation& request)
    {
        AssetInstantiationOptions options {
            .parent = request.parent,
            .beforeSibling = request.beforeSibling,
            .afterSibling = request.afterSibling,
            .keepPosition = request.keepPosition,
            .keepRotation = request.keepRotation,
            .keepScale    = request.keepScale,
        };
        const auto entity = instantiateAssetInScene(ctx, world, request.uuid, options);
        if (entity == entt::null)
            return false;
        return true;
    }

    void SceneHierarchyWindow::drawPendingAssetInstantiationPopup(EditorContext& ctx, vultra::World& world)
    {
        if (m_PendingAssetInstantiation.openPopup)
        {
            ImGui::OpenPopup("Instantiate Sub Mesh Asset");
            m_PendingAssetInstantiation.openPopup = false;
        }

        const ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings;
        if (ImGui::BeginPopupModal("Instantiate Sub Mesh Asset", nullptr, flags))
        {
            ImGui::TextUnformatted("Keep imported transform channels:");
            ImGui::Spacing();
            ImGui::Checkbox("Position", &m_PendingAssetInstantiation.keepPosition);
            ImGui::Checkbox("Rotation", &m_PendingAssetInstantiation.keepRotation);
            ImGui::Checkbox("Scale", &m_PendingAssetInstantiation.keepScale);
            ImGui::Spacing();

            if (ImGui::Button("Instantiate", ImVec2(110.0f, 0.0f)))
            {
                const auto request          = m_PendingAssetInstantiation;
                m_PendingAssetInstantiation = {};
                (void)completeAssetInstantiation(ctx, world, request);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(90.0f, 0.0f)))
            {
                m_PendingAssetInstantiation = {};
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

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
        const float createButtonWidth = ImGui::GetFrameHeight();
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + ImGui::GetStyle().ItemSpacing.x,
                                 ImGui::GetWindowContentRegionMax().x - createButtonWidth));
        if (ImGui::Button(ICON_MDI_PLUS, ImVec2 {createButtonWidth, 0.0f}))
            ImGui::OpenPopup("SceneHierarchyCreateMenu");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Create entity");
        ImGui::Separator();

        ImGui::TextUnformatted(ICON_MDI_MAGNIFY);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint(
            "##SceneHierarchySearch", "Search entities...", m_SearchBuffer.data(), m_SearchBuffer.size());

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

        if (ImGui::BeginPopup("SceneHierarchyCreateMenu"))
        {
            drawCreateEntityMenu(ctx, world, entt::null);
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupContextWindow("SceneHierarchyContext",
                                           ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
        {
            drawCreateEntityMenu(ctx, world, entt::null);
            ImGui::EndPopup();
        }

        if (ImGui::BeginTable("SceneHierarchyTable",
                              2,
                              ImGuiTableFlags_Resizable | ImGuiTableFlags_NoPadOuterX | ImGuiTableFlags_BordersInnerV |
                                  ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg))
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
            if (ImGui::BeginPopupContextItem("SceneHierarchyEmptySpaceContext", ImGuiPopupFlags_MouseButtonRight))
            {
                drawCreateEntityMenu(ctx, world, entt::null);
                ImGui::EndPopup();
            }
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload =
                        ImGui::AcceptDragDropPayload("VULTRA_ENTITY", ImGuiDragDropFlags_AcceptNoDrawDefaultRect))
                {
                    if (payload->DataSize == sizeof(entt::entity))
                    {
                        entt::entity dropped {};
                        std::memcpy(&dropped, payload->Data, sizeof(entt::entity));
                        if (reg.valid(dropped))
                        {
                            if (auto* status = reg.try_get<vultra::EntityStatusComponent>(dropped);
                                status && status->locked)
                                ctx.state.statusMessage = "Entity is locked.";
                            else
                            {
                                (void)executeSceneHierarchyCommand(ctx,
                                                                   "scene.move_entity",
                                                                   {{"entity", static_cast<uint32_t>(dropped)},
                                                                    {"mode", "root"}});
                            }
                        }
                    }
                }
                acceptAssetDrop(ctx, world, entt::null);
                ImGui::EndDragDropTarget();
            }

            if (!drewAny)
                ui::emptyState(ICON_MDI_CUBE_OFF_OUTLINE, "No Entities", "Right-click or use Create Empty to add one.");

            ImGui::EndTable();
        }

        drawPendingAssetInstantiationPopup(ctx, world);

        ImGui::End();
    }

    bool SceneHierarchyWindow::entityMatchesFilter(vultra::World& world, entt::entity entity, const char* filter) const
    {
        if (filter == nullptr || filter[0] == '\0')
            return true;

        auto& reg = world.registry();
        if (!reg.valid(entity))
            return false;

        const auto  needle        = lowercase(filter);
        const auto* nameComponent = reg.try_get<vultra::NameComponent>(entity);
        const auto  name          = lowercase(nameComponent ? nameComponent->name : "Entity");
        if (name.find(needle) != std::string::npos)
            return true;

        for (auto child = world.firstChild(entity); child != entt::null; child = world.nextSibling(child))
        {
            if (entityMatchesFilter(world, child, filter))
                return true;
        }
        return false;
    }

    void SceneHierarchyWindow::drawEntityNode(EditorContext& ctx,
                                              vultra::World& world,
                                              entt::entity   entity,
                                              const char*    filter)
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
        if (filter != nullptr && filter[0] != '\0')
            flags |= ImGuiTreeNodeFlags_DefaultOpen;

        ImGui::PushID(static_cast<int>(entt::to_integral(entity)));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        const bool   opened  = ImGui::TreeNodeEx("##entity", flags, "%s", label.c_str());
        const ImVec2 itemMin = ImGui::GetItemRectMin();
        const ImVec2 itemMax = ImGui::GetItemRectMax();
        if (ImGui::IsItemClicked() && status.selectable)
        {
            (void)executeSceneHierarchyCommand(
                ctx, "scene.select_entity", {{"entity", static_cast<uint32_t>(entity)}});
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
            const EntityDropMode dropMode  = dropModeForItem(itemMin, itemMax);
            const bool acceptsScenePayload = isDraggingPayload("VULTRA_ENTITY") || isDraggingPayload(kAssetUuidPayload);
            if (acceptsScenePayload)
                drawDropIndicator(itemMin, itemMax, dropMode);

            if (const ImGuiPayload* payload =
                    ImGui::AcceptDragDropPayload("VULTRA_ENTITY", ImGuiDragDropFlags_AcceptNoDrawDefaultRect))
            {
                if (payload->DataSize == sizeof(entt::entity))
                {
                    entt::entity dropped {};
                    std::memcpy(&dropped, payload->Data, sizeof(entt::entity));
                    const bool validChildDrop =
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
                                (void)executeSceneHierarchyCommand(ctx,
                                                                   "scene.move_entity",
                                                                   {{"entity", static_cast<uint32_t>(dropped)},
                                                                    {"mode", "before"},
                                                                    {"sibling", static_cast<uint32_t>(entity)}});
                            }
                            else if (dropMode == EntityDropMode::After)
                            {
                                (void)executeSceneHierarchyCommand(ctx,
                                                                   "scene.move_entity",
                                                                   {{"entity", static_cast<uint32_t>(dropped)},
                                                                    {"mode", "after"},
                                                                    {"sibling", static_cast<uint32_t>(entity)}});
                            }
                            else
                            {
                                (void)executeSceneHierarchyCommand(ctx,
                                                                   "scene.move_entity",
                                                                   {{"entity", static_cast<uint32_t>(dropped)},
                                                                    {"mode", "parent"},
                                                                    {"parent", static_cast<uint32_t>(entity)}});
                            }
                        }
                    }
                }
            }

            const entt::entity targetParent = world.parent(entity);
            if (dropMode == EntityDropMode::Before)
                acceptAssetDrop(ctx, world, targetParent, entity, entt::null);
            else if (dropMode == EntityDropMode::After)
                acceptAssetDrop(ctx, world, targetParent, entt::null, entity);
            else if (!status.locked)
                acceptAssetDrop(ctx, world, entity);
            else
                (void)ImGui::AcceptDragDropPayload(kAssetUuidPayload, ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
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
            if (!status.locked && ImGui::BeginMenu("Create Child"))
            {
                drawCreateEntityMenu(ctx, world, entity);
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Delete"))
            {
                (void)executeSceneHierarchyCommand(
                    ctx, "scene.remove_entity", {{"entity", static_cast<uint32_t>(entity)}});
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
            (void)executeSceneHierarchyCommand(ctx,
                                               "scene.update_component",
                                               {{"entity", static_cast<uint32_t>(entity)},
                                                {"component_kind", "entity_status"},
                                                {"visible", !status.visible}});
        }
        ImGui::SameLine();
        if (ui::iconButton(status.locked ? ICON_MDI_LOCK : ICON_MDI_LOCK_OPEN_VARIANT, "Toggle lock", status.locked))
        {
            (void)executeSceneHierarchyCommand(ctx,
                                               "scene.update_component",
                                               {{"entity", static_cast<uint32_t>(entity)},
                                                {"component_kind", "entity_status"},
                                                {"locked", !status.locked}});
        }

        if (m_RenameEntity == entity &&
            ImGui::BeginPopupModal("Rename Entity", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::InputText("Name", m_RenameBuffer.data(), m_RenameBuffer.size());
            if (ImGui::Button("OK"))
            {
                (void)executeSceneHierarchyCommand(ctx,
                                                   "scene.update_component",
                                                   {{"entity", static_cast<uint32_t>(entity)},
                                                    {"component_kind", "name"},
                                                    {"name", m_RenameBuffer.data()}});
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
            {
                if (entityMatchesFilter(world, child, filter))
                    drawEntityNode(ctx, world, child, filter);
            }
            ImGui::TreePop();
        }

        ImGui::PopID();
    }
} // namespace vultra_app
