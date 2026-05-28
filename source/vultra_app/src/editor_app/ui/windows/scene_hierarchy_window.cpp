#include "editor_app/ui/windows/scene_hierarchy_window.hpp"

#include "common/ui_widgets.hpp"
#include "editor_app/selection.hpp"

#include <IconsMaterialDesignIcons.h>
#include <vultra/function/services/asset_service.hpp>
#include <vultra/function/services/scene_service.hpp>
#include <vultra/function/services/world_service.hpp>
#include <vultra/function/world/components/camera_component.hpp>
#include <vultra/function/world/components/entity_status_component.hpp>
#include <vultra/function/world/components/environment_component.hpp>
#include <vultra/function/world/components/gaussian_splat_component.hpp>
#include <vultra/function/world/components/id_component.hpp>
#include <vultra/function/world/components/light_component.hpp>
#include <vultra/function/world/components/mesh_component.hpp>
#include <vultra/function/world/components/name_component.hpp>
#include <vultra/function/world/components/reflection_probe_component.hpp>
#include <vultra/function/world/components/transform_component.hpp>
#include <vultra/function/world/components/xr_view_component.hpp>
#include <vultra/function/world/world.hpp>

#include <imgui.h>

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
            if (reg.all_of<vultra::XRViewComponent>(entity))
                return ICON_MDI_VIRTUAL_REALITY;
            if (reg.all_of<vultra::CameraComponent>(entity))
                return ICON_MDI_CAMERA;
            if (reg.all_of<vultra::EnvironmentComponent>(entity))
                return ICON_MDI_WEATHER_SUNNY;
            if (reg.all_of<vultra::ReflectionProbeComponent>(entity))
                return ICON_MDI_CUBE_SCAN;
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

        std::string assetNameFromEntry(const vasset::VAssetRegistry::AssetEntry& entry)
        {
            const auto sourceName = std::filesystem::path(entry.sourcePath).stem().generic_string();
            if (!sourceName.empty())
                return sourceName;
            const auto importedName = std::filesystem::path(entry.importedPath).stem().generic_string();
            return importedName.empty() ? "Asset" : importedName;
        }

        struct MeshSubAssetPlacement
        {
            std::string                name;
            vultra::TransformComponent transform;
        };

        struct MeshSubAssetTransformOptions
        {
            bool keepPosition {true};
            bool keepRotation {true};
            bool keepScale {true};
        };

        std::optional<MeshSubAssetPlacement> findMeshSubAssetPlacement(EditorContext&          ctx,
                                                                       const vultra::CoreUUID& meshUuid)
        {
            if (!ctx.services || !meshUuid.valid())
                return std::nullopt;

            auto* assetService = ctx.services->tryGet<vultra::IAssetService>();
            if (!assetService)
                return std::nullopt;

            auto handle = assetService->loadMeshSync(meshUuid);
            if (!handle.ready() || !handle.cpu() || !handle.cpu()->hasDefaultTransform)
                return std::nullopt;

            MeshSubAssetPlacement placement {};
            placement.name               = handle.cpu()->name;
            placement.transform.position = handle.cpu()->defaultPosition;
            placement.transform.rotation = handle.cpu()->defaultRotation;
            placement.transform.scale    = handle.cpu()->defaultScale;
            placement.transform.dirty    = true;
            return placement;
        }

        entt::entity instantiateDroppedAsset(EditorContext&                      ctx,
                                             vultra::World&                      world,
                                             const vultra::CoreUUID&             uuid,
                                             entt::entity                        parent,
                                             const MeshSubAssetTransformOptions* transformOptions = nullptr)
        {
            if (!uuid.valid() || !ctx.services)
                return entt::null;

            auto* assetService = ctx.services->tryGet<vultra::IAssetService>();
            if (!assetService)
                return entt::null;

            const auto entry = assetService->registry().lookup(uuid.native());
            if (entry.type == vasset::VAssetType::eUnknown)
            {
                ctx.state.statusMessage = "Dropped asset is not registered.";
                return entt::null;
            }

            if (entry.type == vasset::VAssetType::eScene || entry.type == vasset::VAssetType::eSceneManifest)
            {
                auto* sceneService = ctx.services->tryGet<vultra::ISceneService>();
                if (!sceneService)
                    return entt::null;

                std::string uri;
                if (!assetService->resolver().resolve(uuid.native(), uri))
                {
                    ctx.state.statusMessage = "Could not resolve scene asset URI.";
                    return entt::null;
                }

                const auto root = sceneService->instantiateScene(world, uri, parent, false);
                if (root != entt::null)
                    ctx.state.statusMessage = "Instantiated prefab: " + assetNameFromEntry(entry);
                return root;
            }

            if (entry.type == vasset::VAssetType::eMesh || entry.type == vasset::VAssetType::eGaussianSplat)
            {
                auto&      reg       = world.registry();
                auto       entity    = parent == entt::null ? world.createEntity() : world.createChild(parent);
                const auto placement = entry.type == vasset::VAssetType::eMesh ?
                                           findMeshSubAssetPlacement(ctx, uuid) :
                                           std::optional<MeshSubAssetPlacement> {};
                const auto name = placement && !placement->name.empty() ? placement->name : assetNameFromEntry(entry);
                reg.emplace<vultra::NameComponent>(entity, vultra::NameComponent {name});
                auto& transform = reg.get_or_emplace<vultra::TransformComponent>(entity);
                if (placement)
                {
                    const auto defaultTransform = transform;
                    transform                   = placement->transform;
                    if (transformOptions)
                    {
                        if (!transformOptions->keepPosition)
                            transform.position = defaultTransform.position;
                        if (!transformOptions->keepRotation)
                            transform.rotation = defaultTransform.rotation;
                        if (!transformOptions->keepScale)
                            transform.scale = defaultTransform.scale;
                        transform.dirty = true;
                    }
                }

                if (entry.type == vasset::VAssetType::eMesh)
                {
                    reg.emplace<vultra::MeshComponent>(entity, vultra::MeshComponent {.mesh = uuid});
                    ctx.state.statusMessage = "Created mesh entity: " + name;
                }
                else
                {
                    reg.emplace<vultra::GaussianSplatComponent>(entity,
                                                                vultra::GaussianSplatComponent {.gaussianSplat = uuid});
                    ctx.state.statusMessage = "Created gaussian splat entity: " + name;
                }
                return entity;
            }

            ctx.state.statusMessage = "Dropped asset type cannot be instantiated in the scene.";
            return entt::null;
        }

        void selectEntityIfPossible(vultra::World& world, entt::entity entity)
        {
            if (entity == entt::null)
                return;
            if (auto* id = world.registry().try_get<vultra::IDComponent>(entity))
                Selection::select(SelectionCategory::Entity, id->uuid);
        }

        bool shouldPromptMeshSubAssetPlacement(EditorContext& ctx, const vultra::CoreUUID& uuid)
        {
            if (!uuid.valid() || !ctx.services)
                return false;

            auto* assetService = ctx.services->tryGet<vultra::IAssetService>();
            if (!assetService)
                return false;

            const auto entry = assetService->registry().lookup(uuid.native());
            return entry.type == vasset::VAssetType::eMesh && findMeshSubAssetPlacement(ctx, uuid).has_value();
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

        entt::entity createSceneEntity(EditorContext& ctx,
                                       vultra::World& world,
                                       entt::entity   parent,
                                       SceneCreateKind kind)
        {
            auto& reg    = world.registry();
            auto  entity = parent == entt::null ? world.createEntity() : world.createChild(parent);
            auto& transform = reg.get_or_emplace<vultra::TransformComponent>(entity);

            const auto setName = [&](const char* name) {
                reg.emplace_or_replace<vultra::NameComponent>(entity, vultra::NameComponent {name});
            };
            const auto addBuiltinMesh = [&](const char* name, uint32_t geometry) {
                setName(name);
                reg.emplace_or_replace<vultra::MeshComponent>(
                    entity, vultra::MeshComponent {.builtinGeometry = geometry});
            };
            const auto addLight = [&](const char* name, uint32_t lightKind) {
                setName(name);
                auto& light     = reg.emplace_or_replace<vultra::LightComponent>(entity);
                light.kind      = lightKind;
                light.intensity = lightKind == 0u ? 8.0f : 4.0f;
                if (lightKind == 3u)
                {
                    light.twoSided = true;
                    light.width    = 2.0f;
                    light.height   = 2.0f;
                }
                if (lightKind == 0u)
                    transform.rotation =
                        glm::quatLookAtRH(glm::normalize(glm::vec3 {-0.35f, -0.8f, -0.25f}),
                                          glm::vec3 {0.0f, 1.0f, 0.0f});
                else
                    transform.position = glm::vec3 {0.0f, 2.0f, 0.0f};
                transform.dirty = true;
            };
            const auto addCamera = [&](const char* name, bool xr) {
                setName(name);
                auto& camera    = reg.emplace_or_replace<vultra::CameraComponent>(entity);
                camera.primary  = !hasPrimaryCamera(world);
                transform.position = glm::vec3 {0.0f, 1.6f, 5.0f};
                transform.rotation = glm::quat(glm::radians(glm::vec3 {-12.0f, 180.0f, 0.0f}));
                transform.dirty    = true;
                if (xr)
                    reg.emplace_or_replace<vultra::XRViewComponent>(entity);
            };

            switch (kind)
            {
                case SceneCreateKind::Empty:
                    setName(parent == entt::null ? "Empty Entity" : "Child Entity");
                    break;
                case SceneCreateKind::Quad:
                    addBuiltinMesh("Quad", 0u);
                    break;
                case SceneCreateKind::Cube:
                    addBuiltinMesh("Cube", 1u);
                    break;
                case SceneCreateKind::Sphere:
                    addBuiltinMesh("Sphere", 2u);
                    break;
                case SceneCreateKind::Capsule:
                    addBuiltinMesh("Capsule", 3u);
                    break;
                case SceneCreateKind::DirectionalLight:
                    addLight("Directional Light", 0u);
                    break;
                case SceneCreateKind::PointLight:
                    addLight("Point Light", 1u);
                    break;
                case SceneCreateKind::SpotLight:
                    addLight("Spot Light", 2u);
                    break;
                case SceneCreateKind::AreaLight:
                    addLight("Area Light", 3u);
                    break;
                case SceneCreateKind::Camera:
                    addCamera("Camera", false);
                    break;
                case SceneCreateKind::XRCamera:
                    addCamera("XR Camera", true);
                    break;
                case SceneCreateKind::Environment:
                    setName("Environment");
                    reg.emplace_or_replace<vultra::EnvironmentComponent>(entity);
                    break;
            }

            selectEntityIfPossible(world, entity);
            ctx.state.sceneDirty    = true;
            ctx.state.statusMessage = "Created " + reg.get<vultra::NameComponent>(entity).name + ".";
            return entity;
        }

        void drawCreateEntityMenu(EditorContext& ctx, vultra::World& world, entt::entity parent)
        {
            if (ImGui::MenuItem(ICON_MDI_CUBE_OUTLINE " Empty Entity"))
                static_cast<void>(createSceneEntity(ctx, world, parent, SceneCreateKind::Empty));

            if (ImGui::BeginMenu(ICON_MDI_SHAPE " Basic Geometry"))
            {
                if (ImGui::MenuItem(ICON_MDI_VECTOR_SQUARE " Quad"))
                    static_cast<void>(createSceneEntity(ctx, world, parent, SceneCreateKind::Quad));
                if (ImGui::MenuItem(ICON_MDI_CUBE " Cube"))
                    static_cast<void>(createSceneEntity(ctx, world, parent, SceneCreateKind::Cube));
                if (ImGui::MenuItem(ICON_MDI_SPHERE " Sphere"))
                    static_cast<void>(createSceneEntity(ctx, world, parent, SceneCreateKind::Sphere));
                if (ImGui::MenuItem(ICON_MDI_CYLINDER " Capsule"))
                    static_cast<void>(createSceneEntity(ctx, world, parent, SceneCreateKind::Capsule));
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu(ICON_MDI_LIGHTBULB_ON_OUTLINE " Light"))
            {
                if (ImGui::MenuItem("Directional Light"))
                    static_cast<void>(createSceneEntity(ctx, world, parent, SceneCreateKind::DirectionalLight));
                if (ImGui::MenuItem("Point Light"))
                    static_cast<void>(createSceneEntity(ctx, world, parent, SceneCreateKind::PointLight));
                if (ImGui::MenuItem("Spot Light"))
                    static_cast<void>(createSceneEntity(ctx, world, parent, SceneCreateKind::SpotLight));
                if (ImGui::MenuItem("Area Light"))
                    static_cast<void>(createSceneEntity(ctx, world, parent, SceneCreateKind::AreaLight));
                ImGui::EndMenu();
            }

            if (ImGui::MenuItem(ICON_MDI_CAMERA " Camera"))
                static_cast<void>(createSceneEntity(ctx, world, parent, SceneCreateKind::Camera));
            if (ImGui::MenuItem(ICON_MDI_VIRTUAL_REALITY " XR Camera"))
                static_cast<void>(createSceneEntity(ctx, world, parent, SceneCreateKind::XRCamera));
            if (ImGui::MenuItem(ICON_MDI_WEATHER_SUNNY " Environment"))
                static_cast<void>(createSceneEntity(ctx, world, parent, SceneCreateKind::Environment));
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
        MeshSubAssetTransformOptions transformOptions {
            .keepPosition = request.keepPosition,
            .keepRotation = request.keepRotation,
            .keepScale    = request.keepScale,
        };
        const auto entity = instantiateDroppedAsset(ctx, world, request.uuid, request.parent, &transformOptions);
        if (entity == entt::null)
            return false;

        if (request.beforeSibling != entt::null)
            world.insertBefore(entity, request.beforeSibling);
        else if (request.afterSibling != entt::null)
            world.insertAfter(entity, request.afterSibling);

        selectEntityIfPossible(world, entity);
        ctx.state.sceneDirty = true;
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
                                world.removeParent(dropped);
                                ctx.state.sceneDirty    = true;
                                ctx.state.statusMessage = "Moved entity to scene root.";
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
                world.destroyRecursive(entity);
                Selection::clear(SelectionCategory::Entity);
                ctx.state.sceneDirty    = true;
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
            status.visible       = !status.visible;
            ctx.state.sceneDirty = true;
        }
        ImGui::SameLine();
        if (ui::iconButton(status.locked ? ICON_MDI_LOCK : ICON_MDI_LOCK_OPEN_VARIANT, "Toggle lock", status.locked))
        {
            status.locked        = !status.locked;
            ctx.state.sceneDirty = true;
        }

        if (m_RenameEntity == entity &&
            ImGui::BeginPopupModal("Rename Entity", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::InputText("Name", m_RenameBuffer.data(), m_RenameBuffer.size());
            if (ImGui::Button("OK"))
            {
                reg.get_or_emplace<vultra::NameComponent>(entity).name = m_RenameBuffer.data();
                m_RenameEntity                                         = entt::null;
                ctx.state.sceneDirty                                   = true;
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
