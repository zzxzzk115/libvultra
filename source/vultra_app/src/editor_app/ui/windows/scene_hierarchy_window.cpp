#include "editor_app/ui/windows/scene_hierarchy_window.hpp"

#include "common/ui_widgets.hpp"
#include "editor_app/editor_app.hpp"
#include "editor_app/editor_history.hpp"
#include "editor_app/scene_asset_instantiation.hpp"
#include "editor_app/selection.hpp"

#include <IconsMaterialDesignIcons.h>
#include <vultra/core/i18n/i18n.hpp>
#include <vultra/function/imgui/imgui_dpi.hpp>
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
#include <vultra/function/world/components/prefab_instance_component.hpp>
#include <vultra/function/world/components/reflection_probe_component.hpp>
#include <vultra/function/world/components/rigid_body_component.hpp>
#include <vultra/function/world/components/sphere_shape_component.hpp>
#include <vultra/function/world/components/transform_component.hpp>
#include <vultra/function/world/components/ui_components.hpp>
#include <vultra/function/world/components/xr_view_component.hpp>
#include <vultra/function/world/world.hpp>

#include <ImGuiFileDialog/ImGuiFileDialog.h>
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

        // Build an "<icon> <translated label>" menu/button caption (the icon stays language-invariant).
        std::string iconLabel(const char* icon, std::string_view key)
        {
            return std::string {icon} + " " + vultra::tr(key);
        }

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
            ParticleEmitter,
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
                case SceneCreateKind::ParticleEmitter:
                    return "particle_emitter";
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
                return {{"ok", false}, {"error", vultra::tr("scene.cmdUnavailable")}};
            auto result = ctx.editor->executeCommand(ctx, name, args);
            if (!result.value("ok", false))
                ctx.state.statusMessage = result.value("error", vultra::tr("scene.cmdFailed"));
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
                drawList->AddRect(itemMin, itemMax, accent, vultra::ui::dp(3.0f), 0, vultra::ui::dp(2.0f));
                return;
            }

            const float y  = mode == EntityDropMode::Before ? itemMin.y : itemMax.y;
            const float x0 = itemMin.x + vultra::ui::dp(2.0f);
            const float x1 = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x - vultra::ui::dp(2.0f);
            drawList->AddLine(ImVec2(x0, y), ImVec2(x1, y), accent, vultra::ui::dp(2.0f));
            drawList->AddCircleFilled(ImVec2(x0, y), vultra::ui::dp(3.0f), accent);
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
            if (ImGui::MenuItem(iconLabel(ICON_MDI_CUBE_OUTLINE, "scene.create.empty").c_str()))
                createSceneEntityCommand(ctx, parent, SceneCreateKind::Empty);

            if (ImGui::BeginMenu(iconLabel(ICON_MDI_SHAPE, "scene.create.basicGeometry").c_str()))
            {
                if (ImGui::MenuItem(iconLabel(ICON_MDI_VECTOR_SQUARE, "scene.create.quad").c_str()))
                    createSceneEntityCommand(ctx, parent, SceneCreateKind::Quad);
                if (ImGui::MenuItem(iconLabel(ICON_MDI_CUBE, "scene.create.cube").c_str()))
                    createSceneEntityCommand(ctx, parent, SceneCreateKind::Cube);
                if (ImGui::MenuItem(iconLabel(ICON_MDI_SPHERE, "scene.create.sphere").c_str()))
                    createSceneEntityCommand(ctx, parent, SceneCreateKind::Sphere);
                if (ImGui::MenuItem(iconLabel(ICON_MDI_CYLINDER, "scene.create.capsule").c_str()))
                    createSceneEntityCommand(ctx, parent, SceneCreateKind::Capsule);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu(iconLabel(ICON_MDI_LIGHTBULB_ON_OUTLINE, "scene.create.light").c_str()))
            {
                if (ImGui::MenuItem(vultra::tr("scene.create.directionalLight")))
                    createSceneEntityCommand(ctx, parent, SceneCreateKind::DirectionalLight);
                if (ImGui::MenuItem(vultra::tr("scene.create.pointLight")))
                    createSceneEntityCommand(ctx, parent, SceneCreateKind::PointLight);
                if (ImGui::MenuItem(vultra::tr("scene.create.spotLight")))
                    createSceneEntityCommand(ctx, parent, SceneCreateKind::SpotLight);
                if (ImGui::MenuItem(vultra::tr("scene.create.areaLight")))
                    createSceneEntityCommand(ctx, parent, SceneCreateKind::AreaLight);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu(iconLabel(ICON_MDI_CREATION, "scene.create.rendering").c_str()))
            {
                if (ImGui::MenuItem(iconLabel(ICON_MDI_CREATION, "scene.create.particleEmitter").c_str()))
                    createSceneEntityCommand(ctx, parent, SceneCreateKind::ParticleEmitter);
                ImGui::EndMenu();
            }

            if (ImGui::MenuItem(iconLabel(ICON_MDI_CAMERA, "scene.create.camera").c_str()))
                createSceneEntityCommand(ctx, parent, SceneCreateKind::Camera);
            if (ImGui::MenuItem(iconLabel(ICON_MDI_VIRTUAL_REALITY, "scene.create.xrCamera").c_str()))
                createSceneEntityCommand(ctx, parent, SceneCreateKind::XRCamera);
            if (ImGui::MenuItem(iconLabel(ICON_MDI_WEATHER_SUNNY, "scene.create.environment").c_str()))
                createSceneEntityCommand(ctx, parent, SceneCreateKind::Environment);

            if (ImGui::BeginMenu(iconLabel(ICON_MDI_APPLICATION, "scene.create.ui").c_str()))
            {
                if (ImGui::MenuItem(iconLabel(ICON_MDI_MONITOR, "scene.create.canvas").c_str()))
                    createSceneEntityCommand(ctx, parent, SceneCreateKind::UiCanvas);
                if (ImGui::MenuItem(iconLabel(ICON_MDI_RECTANGLE_OUTLINE, "scene.create.panel").c_str()))
                    createSceneEntityCommand(ctx, parent, SceneCreateKind::UiPanel);
                if (ImGui::MenuItem(iconLabel(ICON_MDI_FORMAT_TEXT, "scene.create.text").c_str()))
                    createSceneEntityCommand(ctx, parent, SceneCreateKind::UiText);
                if (ImGui::MenuItem(iconLabel(ICON_MDI_IMAGE_OUTLINE, "scene.create.image").c_str()))
                    createSceneEntityCommand(ctx, parent, SceneCreateKind::UiImage);
                if (ImGui::MenuItem(iconLabel(ICON_MDI_GESTURE_TAP_BUTTON, "scene.create.button").c_str()))
                    createSceneEntityCommand(ctx, parent, SceneCreateKind::UiButton);
                if (ImGui::MenuItem(iconLabel(ICON_MDI_CHECKBOX_MARKED_OUTLINE, "scene.create.toggle").c_str()))
                    createSceneEntityCommand(ctx, parent, SceneCreateKind::UiToggle);
                if (ImGui::MenuItem(iconLabel(ICON_MDI_TUNE, "scene.create.slider").c_str()))
                    createSceneEntityCommand(ctx, parent, SceneCreateKind::UiSlider);
                if (ImGui::MenuItem(iconLabel(ICON_MDI_PROGRESS_CHECK, "scene.create.progressBar").c_str()))
                    createSceneEntityCommand(ctx, parent, SceneCreateKind::UiProgressBar);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu(iconLabel(ICON_MDI_ATOM, "scene.create.physics").c_str()))
            {
                if (ImGui::MenuItem(iconLabel(ICON_MDI_CUBE, "scene.create.staticBox").c_str()))
                    createSceneEntityCommand(ctx, parent, SceneCreateKind::StaticBox);
                if (ImGui::MenuItem(iconLabel(ICON_MDI_SPHERE, "scene.create.dynamicSphere").c_str()))
                    createSceneEntityCommand(ctx, parent, SceneCreateKind::DynamicSphere);
                if (ImGui::MenuItem(iconLabel(ICON_MDI_CYLINDER, "scene.create.capsuleRigidBody").c_str()))
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
            ImGui::OpenPopup(vultra::trId("scene.instantiate.title", "InstantiateSubMesh"));
            m_PendingAssetInstantiation.openPopup = false;
        }

        const ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings;
        if (ImGui::BeginPopupModal(vultra::trId("scene.instantiate.title", "InstantiateSubMesh"), nullptr, flags))
        {
            ImGui::TextUnformatted(vultra::tr("scene.instantiate.keepChannels"));
            ImGui::Spacing();
            ImGui::Checkbox(vultra::tr("common.position"), &m_PendingAssetInstantiation.keepPosition);
            ImGui::Checkbox(vultra::tr("common.rotation"), &m_PendingAssetInstantiation.keepRotation);
            ImGui::Checkbox(vultra::tr("common.scale"), &m_PendingAssetInstantiation.keepScale);
            ImGui::Spacing();

            if (ImGui::Button(vultra::tr("scene.instantiate.instantiate"), ImVec2(vultra::ui::dp(110.0f), 0.0f)))
            {
                const auto request          = m_PendingAssetInstantiation;
                m_PendingAssetInstantiation = {};
                (void)completeAssetInstantiation(ctx, world, request);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button(vultra::tr("common.cancel"), ImVec2(vultra::ui::dp(90.0f), 0.0f)))
            {
                m_PendingAssetInstantiation = {};
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    SceneHierarchyWindow::SceneHierarchyWindow() : EditorWindow("Scene Hierarchy", ICON_MDI_FILE_TREE, "window.sceneHierarchy") {}

    void SceneHierarchyWindow::draw(EditorContext& ctx)
    {
        ImGuiWindowFlags windowFlags = 0;
        if (ctx.state.sceneDirty)
            windowFlags |= ImGuiWindowFlags_UnsavedDocument;
        ImGui::Begin(title().c_str(), &m_Open, windowFlags);
        // Editing the scene hierarchy makes the scene the active undo/redo document.
        claimActiveDocument(ctx, ctx.sceneHistory, ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows));
        const bool editingPrefab = !ctx.state.currentEditingPrefab.empty();
        ImGui::TextColored(ImVec4(0.72f, 0.80f, 0.92f, 1.0f), "%s", editingPrefab ? ICON_MDI_CUBE : ICON_MDI_FILE_TREE);
        ImGui::SameLine();
        ImGui::TextUnformatted(editingPrefab ? ctx.state.currentEditingPrefab.c_str() :
                                               ctx.state.currentDefaultScene.c_str());
        const float createButtonWidth = ImGui::GetFrameHeight();
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + ImGui::GetStyle().ItemSpacing.x,
                                 ImGui::GetWindowContentRegionMax().x - createButtonWidth));
        if (ImGui::Button(ICON_MDI_PLUS, ImVec2 {createButtonWidth, 0.0f}))
            ImGui::OpenPopup("SceneHierarchyCreateMenu");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", vultra::tr("scene.createEntity"));
        ImGui::Separator();

        ImGui::TextUnformatted(ICON_MDI_MAGNIFY);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint(
            "##SceneHierarchySearch", vultra::tr("scene.searchHint"), m_SearchBuffer.data(), m_SearchBuffer.size());

        if (!ctx.services)
        {
            ImGui::TextUnformatted(vultra::tr("scene.servicesUnavailable"));
            ImGui::End();
            return;
        }

        auto* worldService = ctx.services->tryGet<vultra::IWorldService>();
        if (!worldService)
        {
            ImGui::TextUnformatted(vultra::tr("scene.worldServiceUnavailable"));
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
            ImGui::TableSetupColumn(vultra::tr("scene.column.entity"), ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn(vultra::tr("scene.column.status"), ImGuiTableColumnFlags_WidthFixed, vultra::ui::dp(86.0f));
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
                                ctx.state.statusMessage = vultra::tr("scene.entityLocked");
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
                ui::emptyState(
                    ICON_MDI_CUBE_OFF_OUTLINE, vultra::tr("scene.empty.title"), vultra::tr("scene.empty.message"));

            ImGui::EndTable();
        }

        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && m_RenameEntity == entt::null &&
            !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete) &&
            Selection::lastCategory() == SelectionCategory::Entity)
        {
            const auto selectedUuid = Selection::lastId();
            for (auto entity : reg.view<vultra::IDComponent>())
            {
                if (reg.get<vultra::IDComponent>(entity).uuid != selectedUuid)
                    continue;
                if (auto* status = reg.try_get<vultra::EntityStatusComponent>(entity); status && status->locked)
                    ctx.state.statusMessage = vultra::tr("scene.entityLocked");
                else
                    (void)executeSceneHierarchyCommand(
                        ctx, "scene.remove_entity", {{"entity", static_cast<uint32_t>(entity)}});
                break;
            }
        }

        drawPendingAssetInstantiationPopup(ctx, world);
        drawCreatePrefabDialog(ctx, world);

        ImGui::End();
    }

    void SceneHierarchyWindow::drawCreatePrefabDialog(EditorContext& ctx, vultra::World& world)
    {
        if (m_OpenCreatePrefabDialog)
        {
            m_OpenCreatePrefabDialog = false;
            const auto root = (ctx.state.currentProject / ctx.state.currentAssetRoot).lexically_normal();
            IGFD::FileDialogConfig config;
            config.path     = root.generic_string();
            config.fileName = m_CreatePrefabDefaultName;
            config.flags    = ImGuiFileDialogFlags_Modal | ImGuiFileDialogFlags_ConfirmOverwrite;
            ImGuiFileDialog::Instance()->OpenDialog("HierarchyCreatePrefab", "Create Prefab", ".vprefab", config);
        }

        if (ImGuiFileDialog::Instance()->Display("HierarchyCreatePrefab",
                                                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings,
                                                 ImVec2(640.0f, 420.0f)))
        {
            if (ImGuiFileDialog::Instance()->IsOk())
            {
                const std::string sel = ImGuiFileDialog::Instance()->GetFilePathName(IGFD_ResultMode_KeepInputFile);
                if (!sel.empty() && world.registry().valid(m_CreatePrefabEntity))
                {
                    (void)executeSceneHierarchyCommand(
                        ctx,
                        "scene.create_prefab",
                        {{"entity", static_cast<uint32_t>(m_CreatePrefabEntity)}, {"path", sel}});
                }
            }
            m_CreatePrefabEntity = entt::null;
            ImGuiFileDialog::Instance()->Close();
        }
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
                                              const char*    filter,
                                              bool           inPrefab)
    {
        auto& reg = world.registry();
        if (!reg.valid(entity))
            return;

        // Prefab content (instance root + all descendants) is tinted blue.
        const bool isPrefabRoot  = reg.all_of<vultra::PrefabInstanceComponent>(entity);
        const bool prefabContent = inPrefab || isPrefabRoot;

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
        if (prefabContent)
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.40f, 0.62f, 1.00f, 1.0f));
        const bool opened = ImGui::TreeNodeEx("##entity", flags, "%s", label.c_str());
        if (prefabContent)
            ImGui::PopStyleColor();
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
            ImGui::OpenPopup(vultra::trId("scene.rename.title", "RenameEntity"));
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
                            ctx.state.statusMessage = vultra::tr("scene.entityLocked");
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
            if (ImGui::MenuItem(vultra::tr("common.rename")))
            {
                m_RenameEntity = entity;
                std::memset(m_RenameBuffer.data(), 0, m_RenameBuffer.size());
                std::memcpy(m_RenameBuffer.data(), name.c_str(), std::min(name.size(), m_RenameBuffer.size() - 1));
                ImGui::OpenPopup(vultra::trId("scene.rename.title", "RenameEntity"));
            }
            if (!status.locked && ImGui::BeginMenu(vultra::tr("scene.context.createChild")))
            {
                drawCreateEntityMenu(ctx, world, entity);
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_MDI_CUBE_OUTLINE "  Create Prefab..."))
            {
                m_CreatePrefabEntity      = entity;
                m_CreatePrefabDefaultName = name + ".vprefab";
                m_OpenCreatePrefabDialog  = true;
            }
            if (isPrefabRoot && ImGui::MenuItem(ICON_MDI_CUBE_OFF_OUTLINE "  Unpack Prefab"))
            {
                (void)executeSceneHierarchyCommand(
                    ctx, "scene.unpack_prefab", {{"entity", static_cast<uint32_t>(entity)}});
            }
            ImGui::Separator();
            if (ImGui::MenuItem(vultra::tr("common.delete")))
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
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + vultra::ui::dp(6.0f));
        if (ui::iconButton(
                status.visible ? ICON_MDI_EYE : ICON_MDI_EYE_OFF, vultra::tr("scene.toggleVisibility"), status.visible))
        {
            (void)executeSceneHierarchyCommand(ctx,
                                               "scene.update_component",
                                               {{"entity", static_cast<uint32_t>(entity)},
                                                {"component_kind", "entity_status"},
                                                {"visible", !status.visible}});
        }
        ImGui::SameLine();
        if (ui::iconButton(
                status.locked ? ICON_MDI_LOCK : ICON_MDI_LOCK_OPEN_VARIANT, vultra::tr("scene.toggleLock"), status.locked))
        {
            (void)executeSceneHierarchyCommand(ctx,
                                               "scene.update_component",
                                               {{"entity", static_cast<uint32_t>(entity)},
                                                {"component_kind", "entity_status"},
                                                {"locked", !status.locked}});
        }

        if (m_RenameEntity == entity &&
            ImGui::BeginPopupModal(vultra::trId("scene.rename.title", "RenameEntity"), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::InputText(vultra::tr("common.name"), m_RenameBuffer.data(), m_RenameBuffer.size());
            if (ImGui::Button(vultra::tr("common.ok")))
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
            if (ImGui::Button(vultra::tr("common.cancel")))
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
                    drawEntityNode(ctx, world, child, filter, prefabContent);
            }
            ImGui::TreePop();
        }

        ImGui::PopID();
    }
} // namespace vultra_app
