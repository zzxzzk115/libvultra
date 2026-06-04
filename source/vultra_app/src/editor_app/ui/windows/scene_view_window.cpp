#include "editor_app/ui/windows/scene_view_window.hpp"

#include "editor_app/editor_history.hpp"
#include "editor_app/scene_thumbnail.hpp"
#include "editor_app/selection.hpp"

#include <IconsMaterialDesignIcons.h>
#include <vultra/core/services/input_service.hpp>
#include <vultra/function/rendering/render_structs.hpp>
#include <vultra/function/services/asset_service.hpp>
#include <vultra/function/services/camera_service.hpp>
#include <vultra/function/services/render_backend_service.hpp>
#include <vultra/function/services/render_service.hpp>
#include <vultra/function/services/world_service.hpp>
#include <vultra/function/world/components/camera_component.hpp>
#include <vultra/function/world/components/entity_status_component.hpp>
#include <vultra/function/world/components/gaussian_splat_component.hpp>
#include <vultra/function/world/components/hierarchy_component.hpp>
#include <vultra/function/world/components/id_component.hpp>
#include <vultra/function/world/components/mesh_component.hpp>
#include <vultra/function/world/components/name_component.hpp>
#include <vultra/function/world/components/transform_component.hpp>
#include <vultra/function/world/components/ui_components.hpp>
#include <vultra/function/world/world.hpp>

#include <ImGuizmo/ImGuizmo.h>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <imoguizmo/imoguizmo.hpp>
#include <stb_image_resize2.h>
#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <limits>
#include <string>
#include <utility>

namespace vultra_app
{
    namespace
    {
        constexpr uint64_t  kRenderTargetReleaseDelayFrames = 3;
        constexpr uint64_t  kRenderTargetResizeStableFrames = 3;
        constexpr float     kOverlayZoomMin                 = 0.5f;
        constexpr float     kOverlayZoomMax                 = 4.0f;
        constexpr float     kOverlayZoomStep                = 0.25f;
        constexpr float     kUiGridStepPx                   = 16.0f;
        constexpr float     kUiRulerMajorStepPx             = 128.0f;
        constexpr float     kUiRulerThicknessPx             = 24.0f;
        constexpr float     kUiRulerPaddingPx               = 10.0f;
        constexpr float     kUi2DZoomMin                    = 0.25f;
        constexpr float     kUi2DZoomMax                    = 8.0f;
        constexpr uint32_t  kSceneThumbnailSize             = 128u;
        constexpr const char* kAssetUuidPayload             = "VULTRA_ASSET_UUID";
        constexpr float     kViewManipulatorSize            = 112.0f;
        constexpr float     kViewManipulatorMargin          = 14.0f;
        constexpr glm::vec3 kWorldUp {0.0f, 1.0f, 0.0f};

        struct Bounds
        {
            glm::vec3 min {std::numeric_limits<float>::max()};
            glm::vec3 max {std::numeric_limits<float>::lowest()};
            bool      valid {false};

            void include(const glm::vec3& p)
            {
                min   = valid ? glm::min(min, p) : p;
                max   = valid ? glm::max(max, p) : p;
                valid = true;
            }

            [[nodiscard]] glm::vec3 center() const { return (min + max) * 0.5f; }
            [[nodiscard]] float     radius() const { return valid ? glm::length((max - min) * 0.5f) : 0.0f; }
        };

        void hashCombine(uint64_t& seed, const uint64_t value)
        {
            seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6u) + (seed >> 2u);
        }

        void hashFloat(uint64_t& seed, const float value)
        {
            uint32_t bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            hashCombine(seed, bits);
        }

        void hashVec3(uint64_t& seed, const glm::vec3& value)
        {
            hashFloat(seed, value.x);
            hashFloat(seed, value.y);
            hashFloat(seed, value.z);
        }

        void hashQuat(uint64_t& seed, const glm::quat& value)
        {
            hashFloat(seed, value.w);
            hashFloat(seed, value.x);
            hashFloat(seed, value.y);
            hashFloat(seed, value.z);
        }

        uint64_t gamePreviewSignature(EditorContext&           ctx,
                                      vultra::World&           world,
                                      const entt::entity       entity,
                                      const uint32_t           renderWidth,
                                      const uint32_t           renderHeight)
        {
            uint64_t seed = 1469598103934665603ull;
            hashCombine(seed, ctx.state.projectGeneration);
            hashCombine(seed, ctx.state.assetFileGeneration);
            hashCombine(seed, ctx.state.sceneContentGeneration);
            hashCombine(seed, renderWidth);
            hashCombine(seed, renderHeight);
            hashCombine(seed, ctx.state.sceneDirty ? 1u : 0u);

            if (entity == entt::null)
                return seed;

            auto& reg = world.registry();
            if (const auto* id = reg.try_get<vultra::IDComponent>(entity))
                hashCombine(seed, static_cast<uint64_t>(std::hash<vultra::CoreUUID> {}(id->uuid)));
            if (const auto* transform = reg.try_get<vultra::TransformComponent>(entity))
            {
                hashVec3(seed, transform->position);
                hashQuat(seed, transform->rotation);
                hashVec3(seed, transform->scale);
            }
            if (const auto* camera = reg.try_get<vultra::CameraComponent>(entity))
            {
                hashCombine(seed, camera->primary ? 1u : 0u);
                hashCombine(seed, static_cast<uint64_t>(camera->priority));
                hashCombine(seed, camera->projection);
                hashFloat(seed, camera->fovYDegrees);
                hashFloat(seed, camera->orthographicHeight);
                hashFloat(seed, camera->zNear);
                hashFloat(seed, camera->zFar);
                hashCombine(seed, camera->clearMode);
                hashFloat(seed, camera->clearColor.r);
                hashFloat(seed, camera->clearColor.g);
                hashFloat(seed, camera->clearColor.b);
                hashFloat(seed, camera->clearColor.a);
                hashCombine(seed, static_cast<uint64_t>(std::hash<std::string> {}(camera->rendererKey)));
            }
            return seed;
        }

        glm::vec3 makeForward(const float yawDegrees, const float pitchDegrees)
        {
            const float yaw   = glm::radians(yawDegrees);
            const float pitch = glm::radians(pitchDegrees);
            return glm::normalize(glm::vec3 {
                std::cos(yaw) * std::cos(pitch),
                std::sin(pitch),
                std::sin(yaw) * std::cos(pitch),
            });
        }

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

        void selectEntityIfPossible(vultra::World& world, entt::entity entity)
        {
            if (entity == entt::null)
                return;
            if (auto* id = world.registry().try_get<vultra::IDComponent>(entity))
                Selection::select(SelectionCategory::Entity, id->uuid);
        }

        std::string assetNameFromEntry(const vasset::VAssetRegistry::AssetEntry& entry)
        {
            const auto sourceName = std::filesystem::path(entry.sourcePath).stem().generic_string();
            if (!sourceName.empty())
                return sourceName;
            const auto importedName = std::filesystem::path(entry.importedPath).stem().generic_string();
            return importedName.empty() ? "Asset" : importedName;
        }

        bool instantiateDroppedAssetAtViewCenter(EditorContext& ctx, const vultra::CoreUUID& uuid)
        {
            if (!ctx.services || !uuid.valid())
                return false;

            auto* assetService = ctx.services->tryGet<vultra::IAssetService>();
            auto* worldService = ctx.services->tryGet<vultra::IWorldService>();
            if (!assetService || !worldService)
                return false;

            const auto entry = assetService->registry().lookup(uuid.native());
            if (entry.type != vasset::VAssetType::eMesh && entry.type != vasset::VAssetType::eGaussianSplat)
            {
                ctx.state.statusMessage = "Dropped asset type cannot be placed in Scene View.";
                return false;
            }

            auto&      world  = worldService->world();
            auto&      reg    = world.registry();
            auto       entity = world.createEntity();
            const auto name   = assetNameFromEntry(entry);
            reg.emplace<vultra::NameComponent>(entity, vultra::NameComponent {name});
            auto& transform    = reg.get_or_emplace<vultra::TransformComponent>(entity);
            transform.position = ctx.state.sceneCamera.valid ?
                                     ctx.state.sceneCamera.position +
                                         ctx.state.sceneCamera.rotation * glm::vec3(0.0f, 0.0f, -3.0f) :
                                     glm::vec3(0.0f);
            transform.dirty    = true;

            if (entry.type == vasset::VAssetType::eMesh)
            {
                reg.emplace<vultra::MeshComponent>(entity, vultra::MeshComponent {.mesh = uuid});
                ctx.state.statusMessage = "Created mesh entity: " + name;
            }
            else
            {
                reg.emplace<vultra::GaussianSplatComponent>(
                    entity, vultra::GaussianSplatComponent {.gaussianSplat = uuid});
                ctx.state.statusMessage = "Created gaussian splat entity: " + name;
            }

            selectEntityIfPossible(world, entity);
            ctx.state.sceneDirty = true;
            if (ctx.history)
                ctx.history->setNextLabel("Drop Asset");
            return true;
        }

        bool currentWindowDockTabVisible()
        {
            const ImGuiWindow* window = ImGui::GetCurrentWindowRead();
            return window && (!window->DockIsActive || window->DockTabIsVisible);
        }

        entt::entity findPrimaryCamera(vultra::World& world)
        {
            auto& reg  = world.registry();
            auto  view = reg.view<vultra::IDComponent, vultra::TransformComponent, vultra::CameraComponent>();

            entt::entity best         = entt::null;
            int          bestPriority = std::numeric_limits<int>::min();
            for (auto e : view)
            {
                const auto& camera = view.get<vultra::CameraComponent>(e);
                if (!camera.primary)
                    continue;
                if (best == entt::null || camera.priority >= bestPriority)
                {
                    best         = e;
                    bestPriority = camera.priority;
                }
            }
            return best;
        }

        entt::entity findEntityByPickingId(vultra::World& world, const uint32_t pickingId)
        {
            if (pickingId == 0u)
                return entt::null;

            auto& reg  = world.registry();
            auto  view = reg.view<vultra::IDComponent>();
            for (auto e : view)
            {
                const auto& id = view.get<vultra::IDComponent>(e).uuid;
                if (vultra::makeEntityPickingId(id) == pickingId)
                    return e;
            }
            return entt::null;
        }

        uint32_t decodePickingId(const std::array<uint8_t, 4>& pixel)
        {
            return static_cast<uint32_t>(pixel[0]) | (static_cast<uint32_t>(pixel[1]) << 8u) |
                   (static_cast<uint32_t>(pixel[2]) << 16u);
        }

        vultra::RenderCamera makeEditorCamera(const glm::vec3&      position,
                                              const float           yaw,
                                              const float           pitch,
                                              const float           fovY,
                                              const float           aspect,
                                              vultra::rhi::Texture* target,
                                              std::string_view      rendererKey,
                                              const uint32_t        clearMode,
                                              const glm::vec4&      clearValue)
        {
            const auto forward = makeForward(yaw, pitch);

            vultra::RenderCamera camera {};
            camera.name        = "Scene View";
            camera.priority    = -100;
            camera.view        = glm::lookAt(position, position + forward, kWorldUp);
            camera.projection  = glm::perspectiveRH_ZO(glm::radians(fovY), std::max(aspect, 0.0001f), 0.05f, 2000.0f);
            camera.zNear       = 0.05f;
            camera.zFar        = 2000.0f;
            camera.fovY        = glm::radians(fovY);
            camera.target      = target;
            camera.clearValue  = clearValue;
            camera.clearMode   = clearMode;
            camera.renderImGui = false;
            camera.rendererKey = rendererKey.empty() ? "universal" : std::string(rendererKey);
            camera.debugEntityIdOutput     = false;
            camera.selectionOutlineEnabled = true;
            return camera;
        }

        vultra::RenderCamera makeUi2DEditorCamera(const float           viewportWidth,
                                                  const float           viewportHeight,
                                                  vultra::rhi::Texture* target,
                                                  std::string_view      rendererKey,
                                                  const glm::vec4&      clearValue)
        {
            const float width  = std::max(viewportWidth, 1.0f);
            const float height = std::max(viewportHeight, 1.0f);

            vultra::RenderCamera camera {};
            camera.name       = "Scene View 2D";
            camera.priority   = -100;
            camera.view       = glm::lookAtRH(glm::vec3 {0.0f, 0.0f, 1000.0f},
                                        glm::vec3 {0.0f, 0.0f, 0.0f},
                                        glm::vec3 {0.0f, 1.0f, 0.0f});
            camera.projection = glm::orthoRH_ZO(-width * 0.5f, width * 0.5f, -height * 0.5f, height * 0.5f, 0.05f, 2000.0f);
            camera.zNear      = 0.05f;
            camera.zFar       = 2000.0f;
            camera.fovY       = 0.0f;
            camera.target     = target;
            camera.clearValue = clearValue;
            camera.clearMode  = 0u;
            camera.renderImGui = false;
            camera.cullingMask = vultra::kRenderLayerUiMask;
            camera.suppressSkybox = true;
            camera.rendererKey = rendererKey.empty() ? "universal" : std::string(rendererKey);
            camera.debugEntityIdOutput     = false;
            camera.selectionOutlineEnabled = true;
            return camera;
        }

        struct EditorCameraSceneSettings
        {
            std::string rendererKey {"universal"};
            uint32_t    clearMode {0};
            glm::vec4   clearValue {0.035f, 0.04f, 0.052f, 1.0f};
        };

        EditorCameraSceneSettings editorCameraSceneSettings(EditorContext& ctx)
        {
            auto* worldService = ctx.services ? ctx.services->tryGet<vultra::IWorldService>() : nullptr;
            if (!worldService)
                return {};

            auto& world   = worldService->world();
            auto  primary = findPrimaryCamera(world);
            if (primary == entt::null)
                return {};

            const auto& camera = world.registry().get<vultra::CameraComponent>(primary);
            return {
                .rendererKey = camera.rendererKey.empty() ? "universal" : camera.rendererKey,
                .clearMode   = camera.clearMode,
                .clearValue  = glm::vec4(camera.clearColor.r, camera.clearColor.g, camera.clearColor.b, 1.0f),
            };
        }

        glm::mat4 makeTransformMatrix(const vultra::TransformComponent& transform)
        {
            return glm::translate(glm::mat4 {1.0f}, transform.position) * glm::mat4_cast(transform.rotation) *
                   glm::scale(glm::mat4 {1.0f}, transform.scale);
        }

        glm::mat4 makeWorldTransformMatrix(const entt::registry& reg, const entt::entity entity)
        {
            const auto* transform = reg.try_get<vultra::TransformComponent>(entity);
            if (!transform)
                return glm::mat4 {1.0f};

            const auto  local     = makeTransformMatrix(*transform);
            const auto* hierarchy = reg.try_get<vultra::HierarchyComponent>(entity);
            if (!hierarchy || hierarchy->parent == entt::null || !reg.valid(hierarchy->parent))
                return local;

            return makeWorldTransformMatrix(reg, hierarchy->parent) * local;
        }

        glm::mat4 makeParentWorldTransformMatrix(const entt::registry& reg, const entt::entity entity)
        {
            const auto* hierarchy = reg.try_get<vultra::HierarchyComponent>(entity);
            if (!hierarchy || hierarchy->parent == entt::null || !reg.valid(hierarchy->parent))
                return glm::mat4 {1.0f};
            return makeWorldTransformMatrix(reg, hierarchy->parent);
        }

        vultra::TransformComponent decomposeTransformMatrix(const glm::mat4&                  matrix,
                                                            const vultra::TransformComponent& fallback)
        {
            float translation[3] {};
            float rotation[3] {};
            float scale[3] {};
            ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(matrix), translation, rotation, scale);

            auto out     = fallback;
            out.position = {translation[0], translation[1], translation[2]};
            out.rotation = glm::normalize(glm::quat(glm::radians(glm::vec3 {rotation[0], rotation[1], rotation[2]})));
            out.scale    = {scale[0], scale[1], scale[2]};
            return out;
        }

        bool setLocalTransformFromGizmoMatrix(entt::registry&             reg,
                                              const entt::entity          entity,
                                              vultra::TransformComponent& transform,
                                              const glm::mat4&            worldMatrix,
                                              const SceneViewWindow::Tool tool)
        {
            const auto parentWorld   = makeParentWorldTransformMatrix(reg, entity);
            const auto parentInverse = glm::inverse(parentWorld);
            switch (tool)
            {
                case SceneViewWindow::Tool::Move:
                    transform.position = glm::vec3(parentInverse * glm::vec4(glm::vec3(worldMatrix[3]), 1.0f));
                    break;
                case SceneViewWindow::Tool::Rotate:
                    transform.rotation = decomposeTransformMatrix(parentInverse * worldMatrix, transform).rotation;
                    break;
                case SceneViewWindow::Tool::Scale:
                    transform.scale = decomposeTransformMatrix(parentInverse * worldMatrix, transform).scale;
                    break;
                case SceneViewWindow::Tool::Select:
                    return false;
            }

            transform.dirty = true;
            return true;
        }

        bool isDescendantOrSelf(const vultra::World& world, entt::entity entity, entt::entity root)
        {
            for (auto e = entity; e != entt::null; e = world.parent(e))
            {
                if (e == root)
                    return true;
            }
            return false;
        }

        void includeMeshWorldBounds(Bounds& bounds, const vasset::VMesh& mesh, const glm::mat4& worldMatrix)
        {
            if (mesh.hasLocalBounds)
            {
                const glm::vec3 min = mesh.localBoundsMin;
                const glm::vec3 max = mesh.localBoundsMax;
                for (uint32_t corner = 0; corner < 8u; ++corner)
                {
                    const glm::vec3 p {
                        (corner & 1u) ? max.x : min.x,
                        (corner & 2u) ? max.y : min.y,
                        (corner & 4u) ? max.z : min.z,
                    };
                    bounds.include(glm::vec3(worldMatrix * glm::vec4(p, 1.0f)));
                }
                return;
            }

            for (const auto& p : mesh.positions)
                bounds.include(glm::vec3(worldMatrix * glm::vec4(p, 1.0f)));
        }

        Bounds computeEntityFocusBounds(vultra::World& world, vultra::IAssetService& assets, entt::entity root)
        {
            Bounds bounds;
            if (root == entt::null)
                return bounds;

            auto& reg      = world.registry();
            auto  meshView = reg.view<vultra::TransformComponent, vultra::MeshComponent>();
            for (auto e : meshView)
            {
                if (!isDescendantOrSelf(world, e, root))
                    continue;

                const auto& meshComponent = meshView.get<vultra::MeshComponent>(e);
                if (!meshComponent.mesh.valid())
                    continue;

                auto mesh = assets.loadMeshAsync(meshComponent.mesh);
                if (!mesh.cpu())
                    continue;

                const auto worldMatrix = makeWorldTransformMatrix(reg, e);
                includeMeshWorldBounds(bounds, *mesh.cpu(), worldMatrix);
            }

            if (!bounds.valid)
            {
                if (const auto* transform = reg.try_get<vultra::TransformComponent>(root))
                {
                    const auto worldMatrix = makeWorldTransformMatrix(reg, root);
                    const auto point       = glm::vec3(worldMatrix * glm::vec4 {0.0f, 0.0f, 0.0f, 1.0f});
                    bounds.include(point);

                    const glm::vec3 extent = glm::max(glm::abs(transform->scale), glm::vec3 {0.5f});
                    bounds.include(point - extent);
                    bounds.include(point + extent);
                }
            }

            return bounds;
        }

        glm::mat4 makeGameProjection(const vultra::CameraComponent& camera, const float aspect)
        {
            const float zNear = std::max(camera.zNear, 0.0001f);
            const float zFar  = std::max(camera.zFar, zNear + 0.0001f);
            if (camera.projection == 1u)
            {
                const float height = std::max(camera.orthographicHeight, 0.0001f);
                const float width  = height * std::max(aspect, 0.0001f);
                return glm::orthoRH_ZO(-width * 0.5f, width * 0.5f, -height * 0.5f, height * 0.5f, zNear, zFar);
            }

            return glm::perspectiveRH_ZO(glm::radians(camera.fovYDegrees), std::max(aspect, 0.0001f), zNear, zFar);
        }

        vultra::RenderCamera makeGameOverlayCamera(vultra::World&        world,
                                                   const entt::entity    entity,
                                                   const float           aspect,
                                                   vultra::rhi::Texture* target)
        {
            auto& reg    = world.registry();
            auto& id     = reg.get<vultra::IDComponent>(entity);
            auto& camera = reg.get<vultra::CameraComponent>(entity);

            vultra::RenderCamera out {};
            out.uuid = id.uuid;
            out.name =
                reg.all_of<vultra::NameComponent>(entity) ? reg.get<vultra::NameComponent>(entity).name : "Game View";
            out.priority                = camera.priority;
            out.view                    = glm::inverse(makeWorldTransformMatrix(reg, entity));
            out.projection              = makeGameProjection(camera, aspect);
            out.zNear                   = std::max(camera.zNear, 0.0001f);
            out.zFar                    = std::max(camera.zFar, out.zNear + 0.0001f);
            out.fovY                    = glm::radians(camera.fovYDegrees);
            out.target                  = target;
            out.clearValue              = camera.clearColor;
            out.clearValue.a            = 1.0f;
            out.clearMode               = camera.clearMode;
            out.renderImGui             = false;
            out.debugEntityIdOutput     = false;
            out.selectionOutlineEnabled = false;
            out.rendererKey             = camera.rendererKey.empty() ? "universal" : camera.rendererKey;
            return out;
        }

        ImGuizmo::OPERATION toGizmoOperation(const SceneViewWindow::Tool tool)
        {
            switch (tool)
            {
                case SceneViewWindow::Tool::Move:
                    return ImGuizmo::TRANSLATE;
                case SceneViewWindow::Tool::Rotate:
                    return ImGuizmo::ROTATE;
                case SceneViewWindow::Tool::Scale:
                    return ImGuizmo::SCALE;
                case SceneViewWindow::Tool::Transform:
                    return ImGuizmo::UNIVERSAL;
                case SceneViewWindow::Tool::Rect:
                case SceneViewWindow::Tool::Select:
                    return ImGuizmo::TRANSLATE;
            }
            return ImGuizmo::TRANSLATE;
        }

        void tooltip(const char* text)
        {
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("%s", text);
        }

        ImVec2 uiPoint(const ImVec2& canvasMin, const float scale, const glm::vec2 p)
        {
            return {canvasMin.x + p.x * scale, canvasMin.y + p.y * scale};
        }

        float uiDrawLineCoord(const float value)
        {
            return std::floor(value) + 0.5f;
        }

        ImVec2 uiDrawLinePoint(const ImVec2& value)
        {
            return {uiDrawLineCoord(value.x), uiDrawLineCoord(value.y)};
        }

        ImVec2 uiCanvasLayoutMin(const ImVec2& viewportMin)
        {
            const float inset = kUiRulerThicknessPx + kUiRulerPaddingPx;
            return {viewportMin.x + inset, viewportMin.y + inset};
        }

        ImVec2 uiCanvasLayoutSize(const ImVec2& viewportSize)
        {
            const float inset = kUiRulerThicknessPx + kUiRulerPaddingPx;
            return {std::max(1.0f, viewportSize.x - inset), std::max(1.0f, viewportSize.y - inset)};
        }

        ImVec2 uiCanvasMin(const ImVec2&    viewportMin,
                           const ImVec2&    viewportSize,
                           const glm::vec2& reference,
                           const float      scale,
                           const glm::vec2& viewPanPx)
        {
            (void)viewportSize;
            (void)reference;
            (void)scale;
            const ImVec2 layoutMin = uiCanvasLayoutMin(viewportMin);
            return {layoutMin.x + viewPanPx.x, layoutMin.y + viewPanPx.y};
        }

        ImVec2 uiPointFlippedY(const ImVec2&    viewportMin,
                               const ImVec2&    viewportSize,
                               const glm::vec2& reference,
                               const float      scale,
                               const glm::vec2& viewPanPx,
                               const glm::vec2  p)
        {
            (void)reference;
            const ImVec2 canvasMin = uiCanvasMin(viewportMin, viewportSize, reference, scale, viewPanPx);
            return {canvasMin.x + p.x * scale, canvasMin.y + p.y * scale};
        }

        glm::vec2 screenDeltaToUi(const ImVec2& delta, const float scale)
        {
            return {delta.x / scale, delta.y / scale};
        }

        glm::vec2 screenPointToUi(const ImVec2&    viewportMin,
                                  const ImVec2&    viewportSize,
                                  const glm::vec2& reference,
                                  const float      scale,
                                  const glm::vec2& viewPanPx,
                                  const ImVec2&    p)
        {
            const ImVec2 canvasMin = uiCanvasMin(viewportMin, viewportSize, reference, scale, viewPanPx);
            return {(p.x - canvasMin.x) / scale, (p.y - canvasMin.y) / scale};
        }

        glm::vec2 rotateUiPoint(const glm::vec2& p, const float degrees)
        {
            const float r = glm::radians(degrees);
            const float c = std::cos(r);
            const float s = std::sin(r);
            return {p.x * c - p.y * s, p.x * s + p.y * c};
        }

        glm::vec2 inverseRotateUiPoint(const glm::vec2& p, const float degrees)
        {
            return rotateUiPoint(p, -degrees);
        }

        std::array<ImVec2, 4> uiRectScreenCorners(const ImVec2&    viewportMin,
                                                  const ImVec2&    viewportSize,
                                                  const glm::vec2& canvasReferencePx,
                                                  const float      scale,
                                                  const glm::vec2& viewPanPx,
                                                  const glm::vec2& pivotPx,
                                                  const glm::vec2& sizePx,
                                                  const vultra::RectTransformComponent& rect)
        {
            const glm::vec2 scaledSize = glm::max(sizePx * rect.scale, glm::vec2 {1.0f});
            const glm::vec2 localMin   = -scaledSize * rect.pivot;
            const glm::vec2 localMax   = localMin + scaledSize;
            const glm::vec2 points[] {
                {localMin.x, localMin.y},
                {localMax.x, localMin.y},
                {localMax.x, localMax.y},
                {localMin.x, localMax.y},
            };

            std::array<ImVec2, 4> out {};
            for (size_t i = 0; i < out.size(); ++i)
                out[i] = uiPointFlippedY(
                    viewportMin,
                    viewportSize,
                    canvasReferencePx,
                    scale,
                    viewPanPx,
                    pivotPx + rotateUiPoint(points[i], rect.rotationDegrees));
            return out;
        }

        bool screenRectContains(const ImVec2& min, const ImVec2& max, const ImVec2& p)
        {
            return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y;
        }

        float screenDistance(const ImVec2& a, const ImVec2& b)
        {
            const float dx = a.x - b.x;
            const float dy = a.y - b.y;
            return std::sqrt(dx * dx + dy * dy);
        }

        float uiCanvasViewportScale(const vultra::CanvasComponent& canvas, const ImVec2& viewportSize, const float viewZoom)
        {
            const glm::vec2 reference = glm::max(canvas.referenceResolutionPx, glm::vec2 {1.0f});
            const ImVec2 layoutSize = uiCanvasLayoutSize(viewportSize);
            float baseScale = 1.0f;
            if (canvas.scaleMode == 1u)
                baseScale = std::min(layoutSize.x / reference.x, layoutSize.y / reference.y);
            return std::max(0.001f, baseScale * std::clamp(viewZoom, kUi2DZoomMin, kUi2DZoomMax));
        }

        bool uiEntityVisible(const entt::registry& reg, const entt::entity entity)
        {
            if (const auto* status = reg.try_get<vultra::EntityStatusComponent>(entity))
                return status->active && status->visible;
            return true;
        }

        float snapUiValue(const float value, const bool enabled)
        {
            return enabled ? std::round(value / kUiGridStepPx) * kUiGridStepPx : value;
        }

        glm::vec2 snapUiVec2(const glm::vec2& value, const bool enabled)
        {
            return {snapUiValue(value.x, enabled), snapUiValue(value.y, enabled)};
        }

        void resolveUiRectTopLeft(const vultra::RectTransformComponent& rect,
                                  const glm::vec2&                     parentMinPx,
                                  const glm::vec2&                     parentSizePx,
                                  glm::vec2&                           outMinPx,
                                  glm::vec2&                           outSizePx,
                                  glm::vec2&                           outPivotPx)
        {
            const glm::vec2 anchorMin = parentMinPx + parentSizePx * rect.anchorMin;
            const glm::vec2 anchorMax = parentMinPx + parentSizePx * rect.anchorMax;
            outSizePx                 = glm::max((anchorMax - anchorMin) + rect.sizeDeltaPx, glm::vec2 {1.0f});
            outPivotPx                = anchorMin + rect.anchoredPositionPx;
            outMinPx                  = outPivotPx - outSizePx * rect.pivot;
        }

        void drawUiGridAndRulers(const ImVec2&    viewportMin,
                                 const ImVec2&    viewportSize,
                                 const glm::vec2& reference,
                                 const float      scale,
                                 const glm::vec2& viewPanPx,
                                 const bool       showGrid,
                                 ImDrawList*      drawList)
        {
            const ImVec2 layoutMin = uiCanvasLayoutMin(viewportMin);
            const ImVec2 layoutDrawMin = uiDrawLinePoint(layoutMin);
            const ImVec2 viewportMax {viewportMin.x + viewportSize.x, viewportMin.y + viewportSize.y};
            const ImVec2 canvasMin = uiCanvasMin(viewportMin, viewportSize, reference, scale, viewPanPx);
            const ImVec2 canvasMax {canvasMin.x + reference.x * scale, canvasMin.y + reference.y * scale};
            const ImVec2 canvasDrawMin = uiDrawLinePoint(canvasMin);
            const ImVec2 canvasDrawMax = uiDrawLinePoint(canvasMax);
            if (showGrid)
            {
                const ImVec2 clipMin {std::max(canvasDrawMin.x, layoutDrawMin.x), std::max(canvasDrawMin.y, layoutDrawMin.y)};
                const ImVec2 clipMax {std::min(canvasDrawMax.x, viewportMax.x), std::min(canvasDrawMax.y, viewportMax.y)};
                if (clipMin.x < clipMax.x && clipMin.y < clipMax.y)
                {
                    drawList->PushClipRect(clipMin, clipMax, true);
                    for (float x = 0.0f; x <= reference.x + 0.5f; x += kUiGridStepPx)
                    {
                        const bool major = std::fmod(x, kUiRulerMajorStepPx) < 0.5f;
                        const float sx   = uiDrawLineCoord(canvasMin.x + x * scale);
                        drawList->AddLine(ImVec2 {sx, canvasDrawMin.y},
                                          ImVec2 {sx, canvasDrawMax.y},
                                          major ? IM_COL32(130, 170, 210, 58) : IM_COL32(255, 255, 255, 20),
                                          1.0f);
                    }
                    for (float y = 0.0f; y <= reference.y + 0.5f; y += kUiGridStepPx)
                    {
                        const bool major = std::fmod(y, kUiRulerMajorStepPx) < 0.5f;
                        const float sy   = uiDrawLineCoord(canvasMin.y + y * scale);
                        drawList->AddLine(ImVec2 {canvasDrawMin.x, sy},
                                          ImVec2 {canvasDrawMax.x, sy},
                                          major ? IM_COL32(130, 170, 210, 58) : IM_COL32(255, 255, 255, 20),
                                          1.0f);
                    }
                    drawList->PopClipRect();
                }
            }

            const ImVec2 topMin {layoutDrawMin.x, layoutDrawMin.y - kUiRulerThicknessPx};
            const ImVec2 leftMin {layoutDrawMin.x - kUiRulerThicknessPx, layoutDrawMin.y};
            drawList->AddRectFilled(topMin, ImVec2 {viewportMax.x, layoutDrawMin.y}, IM_COL32(18, 22, 28, 210));
            drawList->AddRectFilled(leftMin, ImVec2 {layoutDrawMin.x, viewportMax.y}, IM_COL32(18, 22, 28, 210));
            for (float x = 0.0f; x <= reference.x + 0.5f; x += kUiRulerMajorStepPx)
            {
                const float sx = uiDrawLineCoord(canvasMin.x + x * scale);
                if (sx < layoutDrawMin.x || sx > viewportMax.x)
                    continue;
                char label[32] {};
                std::snprintf(label, sizeof(label), "%.0f", x);
                drawList->AddLine(ImVec2 {sx, layoutDrawMin.y - 8.0f},
                                  ImVec2 {sx, layoutDrawMin.y},
                                  IM_COL32(170, 190, 210, 180));
                drawList->AddText(ImVec2 {sx + 3.0f, layoutDrawMin.y - 19.0f}, IM_COL32(190, 204, 218, 220), label);
            }
            for (float y = 0.0f; y <= reference.y + 0.5f; y += kUiRulerMajorStepPx)
            {
                const float sy = uiDrawLineCoord(canvasMin.y + y * scale);
                if (sy < layoutDrawMin.y || sy > viewportMax.y)
                    continue;
                char label[32] {};
                std::snprintf(label, sizeof(label), "%.0f", y);
                drawList->AddLine(ImVec2 {layoutDrawMin.x - 8.0f, sy},
                                  ImVec2 {layoutDrawMin.x, sy},
                                  IM_COL32(170, 190, 210, 180));
                drawList->AddText(ImVec2 {leftMin.x + 3.0f, sy + 3.0f}, IM_COL32(190, 204, 218, 220), label);
            }
            drawList->AddRectFilled(ImVec2 {layoutDrawMin.x - kUiRulerThicknessPx, layoutDrawMin.y - kUiRulerThicknessPx},
                                    layoutDrawMin,
                                    IM_COL32(18, 22, 28, 230));
        }

        struct UiEditorHit
        {
            entt::entity entity {entt::null};
            entt::entity canvas {entt::null};
            int          sortOrder {0};
            uint32_t     depth {0};
        };

        void hitTestUiEntity(vultra::World&      world,
                             const entt::entity  entity,
                             const ImVec2&       viewportMin,
                             const ImVec2&       viewportSize,
                             const glm::vec2&    canvasReferencePx,
                             const glm::vec2&    parentMinPx,
                             const glm::vec2&    parentSizePx,
                             const float         scale,
                             const glm::vec2&    viewPanPx,
                             const entt::entity  canvasEntity,
                             const int           sortOrder,
                             const uint32_t      depth,
                             const ImVec2&       mouse,
                             std::optional<UiEditorHit>& best)
        {
            auto& reg = world.registry();
            auto* rect = reg.try_get<vultra::RectTransformComponent>(entity);
            if (!rect || !uiEntityVisible(reg, entity))
                return;

            glm::vec2 minPx {};
            glm::vec2 sizePx {};
            glm::vec2 pivotPx {};
            resolveUiRectTopLeft(*rect, parentMinPx, parentSizePx, minPx, sizePx, pivotPx);
            const glm::vec2 mouseUi    = screenPointToUi(viewportMin, viewportSize, canvasReferencePx, scale, viewPanPx, mouse);
            const glm::vec2 localMouse = inverseRotateUiPoint(mouseUi - pivotPx, rect->rotationDegrees);
            const glm::vec2 scaledSize = glm::max(sizePx * rect->scale, glm::vec2 {1.0f});
            const glm::vec2 localMin   = -scaledSize * rect->pivot;
            const glm::vec2 localMax   = localMin + scaledSize;
            if (localMouse.x >= localMin.x && localMouse.x <= localMax.x && localMouse.y >= localMin.y &&
                localMouse.y <= localMax.y)
            {
                if (!best || sortOrder > best->sortOrder || (sortOrder == best->sortOrder && depth >= best->depth))
                    best = UiEditorHit {entity, canvasEntity, sortOrder, depth};
            }

            const glm::vec2 maxPx = minPx + sizePx * rect->scale;
            for (auto child = world.firstChild(entity); child != entt::null; child = world.nextSibling(child))
                hitTestUiEntity(world,
                                child,
                                viewportMin,
                                viewportSize,
                                canvasReferencePx,
                                minPx,
                                maxPx - minPx,
                                scale,
                                viewPanPx,
                                canvasEntity,
                                sortOrder,
                                depth + 1u,
                                mouse,
                                best);
        }

        std::optional<UiEditorHit> hitTestUiCanvases(vultra::World& world,
                                                     const ImVec2&  viewportMin,
                                                     const ImVec2&  viewportSize,
                                                     const float    viewZoom,
                                                     const glm::vec2& viewPanPx,
                                                     const ImVec2&  mouse)
        {
            auto& reg = world.registry();
            std::optional<UiEditorHit> best;
            for (auto canvasEntity : reg.view<vultra::CanvasComponent>())
            {
                const auto& canvas = reg.get<vultra::CanvasComponent>(canvasEntity);
                if (!canvas.enabled || !uiEntityVisible(reg, canvasEntity))
                    continue;
                const glm::vec2 reference = glm::max(canvas.referenceResolutionPx, glm::vec2 {1.0f});
                const float fit = uiCanvasViewportScale(canvas, viewportSize, viewZoom);
                const ImVec2 canvasMin = uiCanvasMin(viewportMin, viewportSize, reference, fit, viewPanPx);
                const ImVec2 canvasMax {canvasMin.x + reference.x * fit, canvasMin.y + reference.y * fit};
                if (screenRectContains(canvasMin, canvasMax, mouse))
                    best = UiEditorHit {canvasEntity, canvasEntity, canvas.sortOrder, 0u};
                for (auto child = world.firstChild(canvasEntity); child != entt::null; child = world.nextSibling(child))
                    hitTestUiEntity(world,
                                    child,
                                    viewportMin,
                                    viewportSize,
                                    reference,
                                    glm::vec2 {0.0f},
                                    reference,
                                    fit,
                                    viewPanPx,
                                    canvasEntity,
                                    canvas.sortOrder,
                                    1u,
                                    mouse,
                                    best);
            }
            return best;
        }

        void drawUiEntityOverlay(EditorContext&               ctx,
                                 vultra::World&               world,
                                 const entt::entity           entity,
                                 const ImVec2&                viewportMin,
                                 const ImVec2&                viewportSize,
                                 const glm::vec2              canvasReferencePx,
                                 const glm::vec2              parentMinPx,
                                 const glm::vec2              parentSizePx,
                                 const float                  scale,
                                 const glm::vec2              viewPanPx,
                                 const entt::entity           selectedEntity,
                                 const entt::entity           hoveredEntity,
                                 const SceneViewWindow::Tool  tool,
                                 const bool                   allowEdit,
                                 const bool                   snapEnabled,
                                 SceneViewWindow::Ui2DDragState& dragState)
        {
            auto& reg = world.registry();
            auto* rect = reg.try_get<vultra::RectTransformComponent>(entity);
            if (!rect)
                return;

            glm::vec2 minPx {};
            glm::vec2 sizePx {};
            glm::vec2 pivotPx {};
            resolveUiRectTopLeft(*rect, parentMinPx, parentSizePx, minPx, sizePx, pivotPx);
            const glm::vec2 maxPx     = minPx + sizePx * rect->scale;
            const ImVec2    min       = uiPointFlippedY(viewportMin, viewportSize, canvasReferencePx, scale, viewPanPx, minPx);
            const ImVec2    max       = uiPointFlippedY(viewportMin, viewportSize, canvasReferencePx, scale, viewPanPx, maxPx);
            const auto      corners   = uiRectScreenCorners(viewportMin,
                                                            viewportSize,
                                                            canvasReferencePx,
                                                            scale,
                                                            viewPanPx,
                                                            pivotPx,
                                                            sizePx,
                                                            *rect);

            auto* drawList = ImGui::GetWindowDrawList();
            const bool selected = entity == selectedEntity;
            const bool hovered = entity == hoveredEntity && !selected;
            if (hovered)
                drawList->AddPolyline(corners.data(),
                                      static_cast<int>(corners.size()),
                                      IM_COL32(120, 205, 255, 190),
                                      ImDrawFlags_Closed,
                                      1.5f);
            if (selected)
            {
                const bool activeDrag = dragState.operation != SceneViewWindow::Ui2DDragOperation::None &&
                                        reg.all_of<vultra::IDComponent>(entity) &&
                                        dragState.entityId == reg.get<vultra::IDComponent>(entity).uuid;
                drawList->AddPolyline(corners.data(),
                                      static_cast<int>(corners.size()),
                                      activeDrag ? IM_COL32(255, 224, 120, 255) : IM_COL32(255, 180, 48, 255),
                                      ImDrawFlags_Closed,
                                      2.0f);
                const ImVec2 center = uiPointFlippedY(viewportMin, viewportSize, canvasReferencePx, scale, viewPanPx, pivotPx);
                drawList->AddRectFilled(ImVec2(center.x - 5.0f, center.y - 5.0f),
                                        ImVec2(center.x + 5.0f, center.y + 5.0f),
                                        IM_COL32(72, 126, 255, 255));

                const glm::vec2 xAxisEndPx = pivotPx + rotateUiPoint({80.0f / scale, 0.0f}, rect->rotationDegrees);
                const glm::vec2 yAxisEndPx = pivotPx + rotateUiPoint({0.0f, 80.0f / scale}, rect->rotationDegrees);
                const glm::vec2 rotatePx   = pivotPx + rotateUiPoint({0.0f, (sizePx.y * rect->scale.y * (1.0f - rect->pivot.y)) + 42.0f / scale},
                                                                   rect->rotationDegrees);
                const ImVec2 xAxisEnd = uiPointFlippedY(viewportMin, viewportSize, canvasReferencePx, scale, viewPanPx, xAxisEndPx);
                const ImVec2 yAxisEnd = uiPointFlippedY(viewportMin, viewportSize, canvasReferencePx, scale, viewPanPx, yAxisEndPx);
                const ImVec2 rotateHandle = uiPointFlippedY(viewportMin, viewportSize, canvasReferencePx, scale, viewPanPx, rotatePx);
                const bool showMove = tool == SceneViewWindow::Tool::Move || tool == SceneViewWindow::Tool::Transform;
                const bool showRotate = tool == SceneViewWindow::Tool::Rotate || tool == SceneViewWindow::Tool::Transform;
                const bool showScale = tool == SceneViewWindow::Tool::Scale || tool == SceneViewWindow::Tool::Transform;
                const bool showRect = tool == SceneViewWindow::Tool::Rect || tool == SceneViewWindow::Tool::Transform;
                const glm::vec2 scaledSize = glm::max(sizePx * rect->scale, glm::vec2 {1.0f});
                const glm::vec2 localMin   = -scaledSize * rect->pivot;
                const glm::vec2 localMax   = localMin + scaledSize;
                struct ResizeHandle
                {
                    glm::vec2 local;
                    glm::vec2 axis;
                };
                const ResizeHandle handles[] {
                    {{localMin.x, localMin.y}, {-1.0f, -1.0f}},
                    {{(localMin.x + localMax.x) * 0.5f, localMin.y}, {0.0f, -1.0f}},
                    {{localMax.x, localMin.y}, {1.0f, -1.0f}},
                    {{localMax.x, (localMin.y + localMax.y) * 0.5f}, {1.0f, 0.0f}},
                    {{localMax.x, localMax.y}, {1.0f, 1.0f}},
                    {{(localMin.x + localMax.x) * 0.5f, localMax.y}, {0.0f, 1.0f}},
                    {{localMin.x, localMax.y}, {-1.0f, 1.0f}},
                    {{localMin.x, (localMin.y + localMax.y) * 0.5f}, {-1.0f, 0.0f}},
                };
                if (showMove)
                {
                    drawList->AddLine(center, xAxisEnd, IM_COL32(235, 64, 64, 255), 2.0f);
                    drawList->AddLine(center, yAxisEnd, IM_COL32(70, 210, 92, 255), 2.0f);
                    drawList->AddCircleFilled(xAxisEnd, 4.0f, IM_COL32(235, 64, 64, 255), 12);
                    drawList->AddCircleFilled(yAxisEnd, 4.0f, IM_COL32(70, 210, 92, 255), 12);
                }
                if (showRotate)
                {
                    drawList->AddLine(center, rotateHandle, IM_COL32(255, 210, 80, 180), 1.5f);
                    drawList->AddCircle(rotateHandle, 7.0f, IM_COL32(255, 210, 80, 255), 16, 2.0f);
                }
                if (showScale || showRect)
                {
                    for (const auto& handle : handles)
                    {
                        const ImVec2 p = uiPointFlippedY(viewportMin,
                                                         viewportSize,
                                                         canvasReferencePx,
                                                         scale,
                                                         viewPanPx,
                                                         pivotPx + rotateUiPoint(handle.local, rect->rotationDegrees));
                        drawList->AddRectFilled(ImVec2(p.x - 4.0f, p.y - 4.0f),
                                                ImVec2(p.x + 4.0f, p.y + 4.0f),
                                                IM_COL32(36, 43, 52, 255));
                        drawList->AddRect(ImVec2(p.x - 5.0f, p.y - 5.0f),
                                          ImVec2(p.x + 5.0f, p.y + 5.0f),
                                          IM_COL32(255, 180, 48, 255),
                                          0.0f,
                                          0,
                                          1.5f);
                    }
                }

                const auto mouse = ImGui::GetIO().MousePos;
                if (allowEdit && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && reg.all_of<vultra::IDComponent>(entity))
                {
                    const glm::vec2 mouseUi    = screenPointToUi(viewportMin, viewportSize, canvasReferencePx, scale, viewPanPx, mouse);
                    const glm::vec2 localMouse = inverseRotateUiPoint(mouseUi - pivotPx, rect->rotationDegrees);
                    const bool      overRect =
                        localMouse.x >= localMin.x && localMouse.x <= localMax.x && localMouse.y >= localMin.y &&
                        localMouse.y <= localMax.y;
                    bool hitResize = false;
                    glm::vec2 hitAxis {1.0f};
                    for (const auto& handle : handles)
                    {
                        const ImVec2 p = uiPointFlippedY(viewportMin,
                                                         viewportSize,
                                                         canvasReferencePx,
                                                         scale,
                                                         viewPanPx,
                                                         pivotPx + rotateUiPoint(handle.local, rect->rotationDegrees));
                        if (screenDistance(mouse, p) <= 10.0f)
                        {
                            hitResize = true;
                            hitAxis   = handle.axis;
                            break;
                        }
                    }

                    dragState = {};
                    dragState.entityId                = reg.get<vultra::IDComponent>(entity).uuid;
                    dragState.startMouseUi            = mouseUi;
                    dragState.startAnchoredPositionPx = rect->anchoredPositionPx;
                    dragState.startSizeDeltaPx        = rect->sizeDeltaPx;
                    dragState.startScale              = rect->scale;
                    dragState.startRotationDegrees    = rect->rotationDegrees;
                    dragState.scaleAxis               = hitAxis;
                    dragState.moveAxis                = {1.0f, 1.0f};
                    const float rotateDist            = screenDistance(mouse, rotateHandle);
                    const bool hitXAxis = showMove && screenDistance(mouse, xAxisEnd) <= 14.0f;
                    const bool hitYAxis = showMove && screenDistance(mouse, yAxisEnd) <= 14.0f;
                    if (showRotate && rotateDist <= 12.0f)
                    {
                        const glm::vec2 v = mouseUi - pivotPx;
                        if (glm::dot(v, v) > 0.0001f)
                        {
                            dragState.operation         = SceneViewWindow::Ui2DDragOperation::Rotate;
                            dragState.startAngleDegrees = glm::degrees(std::atan2(v.y, v.x));
                        }
                    }
                    else if (showRect && hitResize && (tool == SceneViewWindow::Tool::Rect || tool == SceneViewWindow::Tool::Transform))
                    {
                        dragState.operation = SceneViewWindow::Ui2DDragOperation::Rect;
                    }
                    else if (showScale && hitResize)
                    {
                        dragState.operation = SceneViewWindow::Ui2DDragOperation::Scale;
                    }
                    else if (showMove && (hitXAxis || hitYAxis || overRect))
                    {
                        dragState.operation = SceneViewWindow::Ui2DDragOperation::Move;
                        if (hitXAxis)
                            dragState.moveAxis = {1.0f, 0.0f};
                        else if (hitYAxis)
                            dragState.moveAxis = {0.0f, 1.0f};
                    }

                    if (dragState.operation != SceneViewWindow::Ui2DDragOperation::None)
                        ImGui::SetNextFrameWantCaptureMouse(true);
                }

                if (activeDrag && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
                    dragState = {};

                if (allowEdit && activeDrag && ImGui::IsMouseDown(ImGuiMouseButton_Left))
                {
                    const glm::vec2 mouseUi = screenPointToUi(viewportMin, viewportSize, canvasReferencePx, scale, viewPanPx, mouse);
                    bool            edited  = false;
                    if (dragState.operation == SceneViewWindow::Ui2DDragOperation::Rotate)
                    {
                        const glm::vec2 v = mouseUi - pivotPx;
                        if (glm::dot(v, v) > 0.0001f)
                        {
                            const float angle = glm::degrees(std::atan2(v.y, v.x));
                            rect->rotationDegrees =
                                dragState.startRotationDegrees + (angle - dragState.startAngleDegrees);
                            edited = true;
                        }
                    }
                    else if (dragState.operation == SceneViewWindow::Ui2DDragOperation::Scale)
                    {
                        const glm::vec2 uiDelta = inverseRotateUiPoint(mouseUi - dragState.startMouseUi,
                                                                       dragState.startRotationDegrees);
                        glm::vec2 nextScale = dragState.startScale;
                        if (dragState.scaleAxis.x != 0.0f)
                            nextScale.x += uiDelta.x * dragState.scaleAxis.x / std::max(sizePx.x, 1.0f);
                        if (dragState.scaleAxis.y != 0.0f)
                            nextScale.y += uiDelta.y * dragState.scaleAxis.y / std::max(sizePx.y, 1.0f);
                        rect->scale = glm::max(nextScale, glm::vec2 {0.05f});
                        edited = true;
                    }
                    else if (dragState.operation == SceneViewWindow::Ui2DDragOperation::Rect)
                    {
                        const bool      snap = snapEnabled && !ImGui::GetIO().KeyAlt;
                        const glm::vec2 uiDelta = inverseRotateUiPoint(mouseUi - dragState.startMouseUi,
                                                                       dragState.startRotationDegrees);
                        glm::vec2 nextSize = dragState.startSizeDeltaPx;
                        if (dragState.scaleAxis.x != 0.0f)
                            nextSize.x += uiDelta.x * dragState.scaleAxis.x;
                        if (dragState.scaleAxis.y != 0.0f)
                            nextSize.y += uiDelta.y * dragState.scaleAxis.y;
                        rect->sizeDeltaPx = glm::max(snapUiVec2(nextSize, snap), glm::vec2 {1.0f});
                        edited = true;
                    }
                    else if (dragState.operation == SceneViewWindow::Ui2DDragOperation::Move)
                    {
                        const bool snap = snapEnabled && !ImGui::GetIO().KeyAlt;
                        glm::vec2 delta = mouseUi - dragState.startMouseUi;
                        delta *= dragState.moveAxis;
                        rect->anchoredPositionPx = snapUiVec2(dragState.startAnchoredPositionPx + delta, snap);
                        edited = true;
                    }

                    if (edited)
                    {
                        ctx.state.sceneDirty = true;
                        if (ctx.history)
                            ctx.history->setNextLabel(dragState.operation == SceneViewWindow::Ui2DDragOperation::Move ?
                                                          "Move UI Rect" :
                                                      dragState.operation == SceneViewWindow::Ui2DDragOperation::Rotate ?
                                                          "Rotate UI Rect" :
                                                      dragState.operation == SceneViewWindow::Ui2DDragOperation::Rect ?
                                                          "Resize UI Rect" :
                                                          "Scale UI Rect");
                    }
                    ImGui::SetNextFrameWantCaptureMouse(true);
                }
            }

            for (auto child = world.firstChild(entity); child != entt::null; child = world.nextSibling(child))
                drawUiEntityOverlay(ctx,
                                    world,
                                    child,
                                    viewportMin,
                                    viewportSize,
                                    canvasReferencePx,
                                    minPx,
                                    maxPx - minPx,
                                    scale,
                                    viewPanPx,
                                    selectedEntity,
                                    hoveredEntity,
                                    tool,
                                    allowEdit,
                                    snapEnabled,
                                    dragState);
        }

        void drawUiCanvasOverlay(EditorContext&              ctx,
                                 vultra::World&              world,
                                 const ImVec2&               viewportMin,
                                 const ImVec2&               viewportSize,
                                 const entt::entity          selectedEntity,
                                 const entt::entity          hoveredEntity,
                                 const SceneViewWindow::Tool tool,
                                 const bool                  allowEdit,
                                 const float                 viewZoom,
                                 const glm::vec2             viewPanPx,
                                 const bool                  showGrid,
                                 const bool                  snapEnabled,
                                 SceneViewWindow::Ui2DDragState& dragState)
        {
            auto& reg = world.registry();
            auto* drawList = ImGui::GetWindowDrawList();
            for (auto canvasEntity : reg.view<vultra::CanvasComponent>())
            {
                const auto& canvas = reg.get<vultra::CanvasComponent>(canvasEntity);
                if (!canvas.enabled)
                    continue;
                const glm::vec2 reference = glm::max(canvas.referenceResolutionPx, glm::vec2 {1.0f});
                const float fit = uiCanvasViewportScale(canvas, viewportSize, viewZoom);
                const ImVec2 canvasMin = uiCanvasMin(viewportMin, viewportSize, reference, fit, viewPanPx);
                const ImVec2 canvasMax {canvasMin.x + reference.x * fit, canvasMin.y + reference.y * fit};
                const ImVec2 canvasDrawMin = uiDrawLinePoint(canvasMin);
                const ImVec2 canvasDrawMax = uiDrawLinePoint(canvasMax);
                drawUiGridAndRulers(viewportMin, viewportSize, reference, fit, viewPanPx, showGrid, drawList);
                drawList->AddRect(canvasDrawMin, canvasDrawMax, IM_COL32(80, 180, 255, 150), 0.0f, 0, 1.5f);
                drawList->AddText(ImVec2(canvasDrawMin.x + 8.0f, canvasDrawMin.y + 8.0f),
                                  IM_COL32(130, 210, 255, 220),
                                  "Canvas");
                for (auto child = world.firstChild(canvasEntity); child != entt::null; child = world.nextSibling(child))
                    drawUiEntityOverlay(ctx,
                                        world,
                                        child,
                                        viewportMin,
                                        viewportSize,
                                        reference,
                                        glm::vec2 {0.0f, 0.0f},
                                        reference,
                                        fit,
                                        viewPanPx,
                                        selectedEntity,
                                        hoveredEntity,
                                        tool,
                                        allowEdit,
                                        snapEnabled,
                                        dragState);
        }
        }

        void applyCameraAlignRequest(AppState&  state,
                                     glm::vec3& cameraPosition,
                                     float&     cameraYaw,
                                     float&     cameraPitch,
                                     float&     cameraFovY)
        {
            if (!state.sceneCameraAlignRequest.pending)
                return;

            const auto& request = state.sceneCameraAlignRequest;
            cameraPosition      = request.position;
            cameraFovY          = request.fovYDegrees;

            const auto forward = glm::normalize(request.rotation * glm::vec3 {0.0f, 0.0f, -1.0f});
            cameraYaw          = glm::degrees(std::atan2(forward.z, forward.x));
            cameraPitch        = glm::degrees(std::asin(std::clamp(forward.y, -1.0f, 1.0f)));

            state.sceneCameraAlignRequest.pending = false;
        }

        void
        applyViewMatrixToCamera(const glm::mat4& view, glm::vec3& cameraPosition, float& cameraYaw, float& cameraPitch)
        {
            const glm::mat4 invView = glm::inverse(view);
            const glm::vec3 forward = glm::normalize(glm::vec3(invView * glm::vec4 {0.0f, 0.0f, -1.0f, 0.0f}));

            cameraPosition = glm::vec3(invView[3]);
            cameraYaw      = glm::degrees(std::atan2(forward.z, forward.x));
            cameraPitch    = glm::degrees(std::asin(std::clamp(forward.y, -1.0f, 1.0f)));
        }

        bool isMouseOverViewManipulator(const ImVec2& viewportMin, const ImVec2& viewportMax)
        {
            const ImVec2 viewportSize {viewportMax.x - viewportMin.x, viewportMax.y - viewportMin.y};
            if (viewportSize.x < kViewManipulatorSize + kViewManipulatorMargin * 2.0f ||
                viewportSize.y < kViewManipulatorSize + kViewManipulatorMargin * 2.0f)
                return false;

            const ImVec2 position {viewportMax.x - kViewManipulatorSize - kViewManipulatorMargin,
                                   viewportMin.y + kViewManipulatorMargin};
            const ImVec2 center {position.x + kViewManipulatorSize * 0.5f, position.y + kViewManipulatorSize * 0.5f};
            const ImVec2 mouse  = ImGui::GetMousePos();
            const float  dx     = mouse.x - center.x;
            const float  dy     = mouse.y - center.y;
            const float  radius = kViewManipulatorSize * 0.5f;
            return dx * dx + dy * dy <= radius * radius;
        }

        glm::vec3 mapArcballPoint(const ImVec2& mouse, const ImVec2& center, const float radius)
        {
            if (radius <= 0.0f)
                return {0.0f, 0.0f, 1.0f};

            glm::vec2 p {(mouse.x - center.x) / radius, (center.y - mouse.y) / radius};
            const float lenSq = glm::dot(p, p);
            if (lenSq > 1.0f)
                p *= 1.0f / std::sqrt(lenSq);

            const float z = std::sqrt(std::max(0.0f, 1.0f - glm::dot(p, p)));
            return glm::normalize(glm::vec3 {p.x, p.y, z});
        }

        glm::quat arcballDelta(const glm::vec3& from, const glm::vec3& to)
        {
            const float d = std::clamp(glm::dot(from, to), -1.0f, 1.0f);
            if (d > 0.9999f)
                return glm::quat {1.0f, 0.0f, 0.0f, 0.0f};

            glm::vec3 axis = glm::cross(from, to);
            if (glm::dot(axis, axis) < 1e-8f)
                axis = std::abs(from.x) < 0.9f ? glm::cross(from, glm::vec3 {1.0f, 0.0f, 0.0f}) :
                                                  glm::cross(from, glm::vec3 {0.0f, 1.0f, 0.0f});

            return glm::normalize(glm::angleAxis(std::acos(d), glm::normalize(axis)));
        }

        bool isMouseInRect(const ImVec2& min, const ImVec2& max)
        {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            return mouse.x >= min.x && mouse.x <= max.x && mouse.y >= min.y && mouse.y <= max.y;
        }

        bool isMouseOverGameOverlay(const EditorContext& ctx,
                                    const ImVec2&        viewportMin,
                                    const ImVec2&        viewportMax,
                                    const float          gameOverlayZoom)
        {
            if (ctx.state.gameViewVisibleLastFrame)
                return false;

            const ImVec2 viewportSize {viewportMax.x - viewportMin.x, viewportMax.y - viewportMin.y};
            if (viewportSize.x < 220.0f || viewportSize.y < 160.0f)
                return false;

            constexpr float aspect    = 16.0f / 9.0f;
            const float     baseWidth = std::min(320.0f, std::max(180.0f, viewportSize.x * 0.22f));
            const float     width =
                std::min(viewportSize.x - 32.0f,
                         baseWidth * std::clamp(gameOverlayZoom, kOverlayZoomMin, kOverlayZoomMax));
            const float  height = width / aspect;
            const ImVec2 padding {14.0f, 14.0f};
            constexpr float controlHeight = 30.0f;
            const ImVec2 panelSize {width + padding.x * 2.0f, height + padding.y * 2.0f + 22.0f + controlHeight};
            const ImVec2 panelMin {viewportMin.x + 16.0f, viewportMax.y - panelSize.y - 16.0f};
            const ImVec2 panelMax {panelMin.x + panelSize.x, panelMin.y + panelSize.y};
            return isMouseInRect(panelMin, panelMax);
        }

        bool supportsScenePicking(EditorContext& ctx)
        {
            auto* backendService = ctx.services ? ctx.services->tryGet<vultra::IRenderBackendService>() : nullptr;
            return backendService &&
                   backendService->renderDevice().getBackendApi() != vultra::rhi::RenderBackendApi::eWebGPU;
        }

    } // namespace

    SceneViewWindow::SceneViewWindow() : EditorWindow("Scene View", ICON_MDI_EYE) {}

    void SceneViewWindow::onClosed(EditorContext& ctx)
    {
        releaseRenderTarget(ctx);
        releasePickingRenderTarget(ctx);
        releaseGameOverlayRenderTarget(ctx);
    }

    void SceneViewWindow::onDestroy(EditorContext& ctx)
    {
        releaseRenderTarget(ctx);
        releasePickingRenderTarget(ctx);
        releaseGameOverlayRenderTarget(ctx);
    }

    bool SceneViewWindow::saveSceneThumbnail(EditorContext& ctx, std::string_view sceneUri)
    {
        if (!ctx.services || sceneUri.empty() || !m_ActiveRenderTarget.texture)
            return false;

        auto* backendService = ctx.services->tryGet<vultra::IRenderBackendService>();
        if (!backendService)
            return false;

        auto pixels = backendService->renderDevice().readTextureRGBA8(*m_ActiveRenderTarget.texture);
        if (!pixels)
            return false;

        const auto extent = m_ActiveRenderTarget.texture->getExtent();
        if (extent.width == 0u || extent.height == 0u)
            return false;

        const auto cropSize = std::min(extent.width, extent.height);
        const auto cropX    = (extent.width - cropSize) / 2u;
        const auto cropY    = (extent.height - cropSize) / 2u;
        std::vector<unsigned char> resized(static_cast<std::size_t>(kSceneThumbnailSize) *
                                           static_cast<std::size_t>(kSceneThumbnailSize) * 4u);
        const auto* cropPixels =
            pixels->data() + (static_cast<std::size_t>(cropY) * extent.width + cropX) * 4u;
        const bool resizedOk = stbir_resize_uint8_srgb(cropPixels,
                                                       static_cast<int>(cropSize),
                                                       static_cast<int>(cropSize),
                                                       static_cast<int>(extent.width * 4u),
                                                       resized.data(),
                                                       static_cast<int>(kSceneThumbnailSize),
                                                       static_cast<int>(kSceneThumbnailSize),
                                                       0,
                                                       STBIR_RGBA);
        if (!resizedOk)
            return false;

        const auto path = sceneThumbnailPath(ctx, sceneUri);
        if (path.empty())
            return false;

        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec)
            return false;

        return stbi_write_png(path.string().c_str(),
                              static_cast<int>(kSceneThumbnailSize),
                              static_cast<int>(kSceneThumbnailSize),
                              4,
                              resized.data(),
                              static_cast<int>(kSceneThumbnailSize * 4u)) != 0;
    }

    void SceneViewWindow::updateFocusAnimation()
    {
        if (!m_FocusActive)
            return;

        const float dt    = std::max(ImGui::GetIO().DeltaTime, 0.0f);
        m_FocusElapsed    = std::min(m_FocusElapsed + dt, m_FocusDuration);
        const float t     = m_FocusDuration > 0.0f ? std::clamp(m_FocusElapsed / m_FocusDuration, 0.0f, 1.0f) : 1.0f;
        const float eased = 1.0f - std::pow(1.0f - t, 3.0f);
        m_CameraPosition  = glm::mix(m_FocusStartPosition, m_FocusTargetPosition, eased);

        if (t >= 1.0f)
            m_FocusActive = false;
    }

    bool SceneViewWindow::focusSelection(EditorContext& ctx, const float aspect)
    {
        if (!ctx.services || Selection::lastCategory() != SelectionCategory::Entity)
            return false;

        auto* worldService = ctx.services->tryGet<vultra::IWorldService>();
        auto* assetService = ctx.services->tryGet<vultra::IAssetService>();
        if (!worldService || !assetService)
            return false;

        auto& world  = worldService->world();
        auto& reg    = world.registry();
        auto  entity = findEntityByUUID(world, Selection::lastId());
        if (entity == entt::null || !reg.valid(entity))
            return false;

        const auto bounds = computeEntityFocusBounds(world, *assetService, entity);
        if (!bounds.valid)
            return false;

        const glm::vec3 center      = bounds.center();
        const float     radius      = std::max(bounds.radius(), 0.25f);
        const float     fovY        = glm::radians(std::clamp(m_CameraFovY, 5.0f, 160.0f));
        const float     safeAspect  = std::max(aspect, 0.0001f);
        const float     tanY        = std::tan(fovY * 0.5f);
        const float     tanX        = tanY * safeAspect;
        const float     fitDistance = radius / std::max(std::min(tanX, tanY), 0.0001f);
        const glm::vec3 forward     = makeForward(m_CameraYaw, m_CameraPitch);

        m_FocusStartPosition  = m_CameraPosition;
        m_FocusTargetPosition = center - forward * std::max(fitDistance * 1.35f, radius + 0.5f);
        m_FocusElapsed        = 0.0f;
        m_FocusDuration       = 0.35f;
        m_FocusActive         = glm::length(m_FocusTargetPosition - m_FocusStartPosition) > 0.0001f;
        if (!m_FocusActive)
            m_CameraPosition = m_FocusTargetPosition;
        return true;
    }

    void SceneViewWindow::draw(EditorContext& ctx)
    {
        resetRenderTargetsForProject(ctx);

        const bool visible =
            ImGui::Begin(title().c_str(), &m_Open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        const bool collapsed            = visible && ImGui::IsWindowCollapsed();
        const bool dockTabVisible       = visible && currentWindowDockTabVisible();
        ctx.state.sceneViewVisible      = visible && !collapsed && dockTabVisible;
        if (!visible || collapsed)
        {
            releaseRenderTarget(ctx);
            releasePickingRenderTarget(ctx);
            ctx.state.sceneCamera.valid = false;
            ImGui::End();
            return;
        }
        if (!dockTabVisible)
        {
            collectRetiredRenderTargets(ctx);
            collectRetiredPickingRenderTargets();
            collectRetiredGameOverlayRenderTargets(ctx);
            ctx.state.sceneCamera.valid = false;
            ImGui::End();
            return;
        }

        const bool explicitSceneViewRequest =
            !ctx.state.sceneViewModeRequest.empty() || !ctx.state.sceneViewToolRequest.empty();
        if (!ctx.state.sceneViewModeRequest.empty())
        {
            if (ctx.state.sceneViewModeRequest == "2d" || ctx.state.sceneViewModeRequest == "ui2d")
                m_ViewMode = ViewMode::Ui2D;
            else if (ctx.state.sceneViewModeRequest == "3d" || ctx.state.sceneViewModeRequest == "view3d")
                m_ViewMode = ViewMode::View3D;
            ctx.state.sceneViewModeRequest.clear();
        }
        if (!ctx.state.sceneViewToolRequest.empty())
        {
            if (ctx.state.sceneViewToolRequest == "select")
                m_Tool = Tool::Select;
            else if (ctx.state.sceneViewToolRequest == "move")
                m_Tool = Tool::Move;
            else if (ctx.state.sceneViewToolRequest == "rotate")
                m_Tool = Tool::Rotate;
            else if (ctx.state.sceneViewToolRequest == "scale")
                m_Tool = Tool::Scale;
            else if (ctx.state.sceneViewToolRequest == "rect")
                m_Tool = Tool::Rect;
            else if (ctx.state.sceneViewToolRequest == "transform")
                m_Tool = Tool::Transform;
            ctx.state.sceneViewToolRequest.clear();
        }
        if (!explicitSceneViewRequest)
        {
            const auto selectedCategory = Selection::lastCategory();
            const auto selectedId       = Selection::lastId();
            if (selectedCategory != m_LastAutoModeSelectionCategory || selectedId != m_LastAutoModeSelectionId)
            {
                m_Ui2DDrag = {};
                m_LastAutoModeSelectionCategory = selectedCategory;
                m_LastAutoModeSelectionId       = selectedId;
                if (selectedCategory == SelectionCategory::Entity && selectedId.valid() && ctx.services)
                {
                    if (auto* worldService = ctx.services->tryGet<vultra::IWorldService>())
                    {
                        auto& world = worldService->world();
                        auto& reg   = world.registry();
                        auto  e     = findEntityByUUID(world, selectedId);
                        if (e != entt::null && reg.valid(e))
                        {
                            const bool selectedUi =
                                reg.all_of<vultra::CanvasComponent>(e) || reg.all_of<vultra::RectTransformComponent>(e);
                            if (selectedUi)
                            {
                                m_ViewMode = ViewMode::Ui2D;
                                m_Tool     = Tool::Transform;
                            }
                            else
                            {
                                m_ViewMode = ViewMode::View3D;
                            }
                        }
                    }
                }
            }
        }

        drawToolbar(ctx);

        ImVec2 avail = ImGui::GetContentRegionAvail();
        avail.x      = std::max(1.0f, avail.x);
        avail.y      = std::max(1.0f, avail.y);
        const bool ui2DMode = m_ViewMode == ViewMode::Ui2D;
        if (!ui2DMode)
            m_Ui2DDrag = {};

        ensureRenderTarget(ctx, static_cast<uint32_t>(avail.x), static_cast<uint32_t>(avail.y));
        initializeCameraFromPrimaryCamera(ctx);
        applyCameraAlignRequest(ctx.state, m_CameraPosition, m_CameraYaw, m_CameraPitch, m_CameraFovY);
        updateFocusAnimation();

        const ImVec2 imagePos = ImGui::GetCursorScreenPos();
        if (m_ActiveRenderTarget.textureId)
            ImGui::Image(m_ActiveRenderTarget.textureId, avail, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f));
        else
            ImGui::InvisibleButton("##SceneViewCanvas", avail);

        const ImVec2 imageMin = ImGui::GetItemRectMin();
        const ImVec2 imageMax = ImGui::GetItemRectMax();
        const bool   hovered  = ImGui::IsItemHovered();
        auto*        dl       = ImGui::GetWindowDrawList();
        if (ui2DMode && !m_ActiveRenderTarget.textureId)
            dl->AddRectFilled(imageMin, imageMax, IM_COL32(36, 43, 52, 255));

        if (ctx.state.scenePicking.readbackPending && m_PickingRenderTarget.texture && supportsScenePicking(ctx))
        {
            if (auto* backendService = ctx.services ? ctx.services->tryGet<vultra::IRenderBackendService>() : nullptr)
            {
                auto& rd        = backendService->renderDevice();
                bool  hitEntity = false;
                if (auto pixel = rd.readTexturePixelRGBA8(
                        *m_PickingRenderTarget.texture, ctx.state.scenePicking.x, ctx.state.scenePicking.y))
                {
                    const uint32_t pickingId = decodePickingId(*pixel);
                    if (auto* worldService = ctx.services->tryGet<vultra::IWorldService>())
                    {
                        auto& world  = worldService->world();
                        auto& reg    = world.registry();
                        auto  entity = findEntityByPickingId(world, pickingId);
                        if (entity != entt::null && reg.valid(entity))
                        {
                            if (auto* id = reg.try_get<vultra::IDComponent>(entity))
                            {
                                ctx.state.selectedSourceAsset.clear();
                                Selection::select(SelectionCategory::Entity, id->uuid);
                                hitEntity = true;
                            }
                        }
                    }
                }
                if (!hitEntity)
                    Selection::clear(SelectionCategory::Entity);
            }
            ctx.state.scenePicking.readbackPending = false;
        }

        if (m_ShowGrid && !ui2DMode)
        {
            constexpr float step = 32.0f;
            for (float x = imageMin.x; x < imageMax.x; x += step)
                dl->AddLine(ImVec2(x, imageMin.y), ImVec2(x, imageMax.y), IM_COL32(255, 255, 255, 18));
            for (float y = imageMin.y; y < imageMax.y; y += step)
                dl->AddLine(ImVec2(imageMin.x, y), ImVec2(imageMax.x, y), IM_COL32(255, 255, 255, 18));
        }
        dl->AddRect(imageMin, imageMax, IM_COL32(90, 100, 118, 255));
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload =
                    ImGui::AcceptDragDropPayload(kAssetUuidPayload, ImGuiDragDropFlags_AcceptNoDrawDefaultRect))
            {
                if (payload->DataSize == sizeof(vultra::CoreUUID))
                {
                    const auto uuid = *static_cast<const vultra::CoreUUID*>(payload->Data);
                    (void)instantiateDroppedAssetAtViewCenter(ctx, uuid);
                }
            }
            ImGui::EndDragDropTarget();
        }
        const float aspect                = avail.x / std::max(avail.y, 1.0f);
        auto*       renderTarget          = m_PendingRenderTarget.texture ?
                                                &*m_PendingRenderTarget.texture :
                                                (m_ActiveRenderTarget.texture ? &*m_ActiveRenderTarget.texture : nullptr);
        const auto  editorSettings        = editorCameraSceneSettings(ctx);
        constexpr std::string_view kUi2DEditorRendererKey {"editor-ui2d"};
        auto        editorCamera          = ui2DMode ?
                                                makeUi2DEditorCamera(avail.x,
                                                                     avail.y,
                                                                     renderTarget,
                                                                     kUi2DEditorRendererKey,
                                                                     m_Ui2DClearColor) :
                                                makeEditorCamera(m_CameraPosition,
                                                                 m_CameraYaw,
                                                                 m_CameraPitch,
                                                                 m_CameraFovY,
                                                                 aspect,
                                                                 renderTarget,
                                                                 editorSettings.rendererKey,
                                                                 editorSettings.clearMode,
                                                                 editorSettings.clearValue);
        const auto applyEditorPlaybackTime = [&ctx](vultra::RenderCamera& camera) {
            camera.overrideFrameTime  = true;
            camera.frameTimeSeconds   = ctx.state.editorPlaying ? ctx.state.editorGameTimeSeconds : 0.0f;
            camera.frameDeltaSeconds  = ctx.state.editorPlaying ? ctx.state.editorGameDeltaSeconds : 0.0f;
        };
        ctx.state.sceneCamera.valid       = true;
        const auto editorCameraWorld      = glm::inverse(editorCamera.view);
        ctx.state.sceneCamera.position    = glm::vec3(editorCameraWorld[3]);
        ctx.state.sceneCamera.rotation    = glm::normalize(glm::quat_cast(glm::inverse(editorCamera.view)));
        ctx.state.sceneCamera.fovYDegrees = m_CameraFovY;

        const bool mouseOverViewManipulator = !ui2DMode && isMouseOverViewManipulator(imageMin, imageMax);
        const bool mouseOverGameOverlay     = isMouseOverGameOverlay(ctx, imageMin, imageMax, m_GameOverlayZoom);
        const bool sceneViewportHovered = hovered && !mouseOverViewManipulator && !mouseOverGameOverlay;
        const bool flyActive = !ui2DMode && sceneViewportHovered && ImGui::IsMouseDown(ImGuiMouseButton_Right);
        const bool sceneWindowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        if (sceneWindowFocused && !flyActive && !ImGui::GetIO().WantTextInput && !ImGuizmo::IsUsing())
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Q))
                m_Tool = Tool::Select;
            if (ImGui::IsKeyPressed(ImGuiKey_W))
                m_Tool = Tool::Move;
            if (ImGui::IsKeyPressed(ImGuiKey_E))
                m_Tool = Tool::Rotate;
            if (ImGui::IsKeyPressed(ImGuiKey_R))
                m_Tool = Tool::Scale;
            if (ImGui::IsKeyPressed(ImGuiKey_T))
                m_Tool = Tool::Rect;
            if (ImGui::IsKeyPressed(ImGuiKey_Y))
                m_Tool = Tool::Transform;
            if (!ui2DMode && ImGui::IsKeyPressed(ImGuiKey_F))
                focusSelection(ctx, avail.x / std::max(avail.y, 1.0f));
        }

        if (!ui2DMode && ctx.services)
        {
            if (auto* input = ctx.services->tryGet<vultra::IInputService>())
            {
                if (flyActive)
                {
                    m_FocusActive      = false;
                    const ImVec2 delta = ImGui::GetIO().MouseDelta;
                    m_CameraYaw += delta.x * 0.12f;
                    m_CameraPitch = std::clamp(m_CameraPitch - delta.y * 0.12f, -89.0f, 89.0f);

                    const auto forward = makeForward(m_CameraYaw, m_CameraPitch);
                    const auto right   = glm::normalize(glm::cross(forward, kWorldUp));
                    glm::vec3  move {};
                    if (input->getKey(vultra::KeyCode::eW))
                        move += forward;
                    if (input->getKey(vultra::KeyCode::eS))
                        move -= forward;
                    if (input->getKey(vultra::KeyCode::eD))
                        move += right;
                    if (input->getKey(vultra::KeyCode::eA))
                        move -= right;
                    if (input->getKey(vultra::KeyCode::eE))
                        move += kWorldUp;
                    if (input->getKey(vultra::KeyCode::eQ))
                        move -= kWorldUp;
                    if (glm::dot(move, move) > 0.0f)
                    {
                        const bool shift =
                            input->getKey(vultra::KeyCode::eLShift) || input->getKey(vultra::KeyCode::eRShift);
                        m_CameraPosition += glm::normalize(move) * (shift ? 0.28f : 0.08f);
                    }
                }

                if (sceneViewportHovered)
                {
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing())
                    {
                        const ImVec2 mouse  = ImGui::GetMousePos();
                        const float  localX = std::clamp(mouse.x - imageMin.x, 0.0f, avail.x - 1.0f);
                        const float  localY = std::clamp(mouse.y - imageMin.y, 0.0f, avail.y - 1.0f);
                        if (supportsScenePicking(ctx))
                        {
                            ctx.state.scenePicking.requested = true;
                            ctx.state.scenePicking.x         = static_cast<uint32_t>(localX);
                            ctx.state.scenePicking.y         = static_cast<uint32_t>(localY);
                        }
                    }

                    if (ImGui::IsMouseDown(ImGuiMouseButton_Middle))
                    {
                        const ImVec2 delta = ImGui::GetIO().MouseDelta;
                        if (delta.x != 0.0f || delta.y != 0.0f)
                        {
                            m_FocusActive       = false;
                            const auto  forward = makeForward(m_CameraYaw, m_CameraPitch);
                            const auto  right   = glm::normalize(glm::cross(forward, kWorldUp));
                            const auto  up      = glm::normalize(glm::cross(right, forward));
                            const float scale   = std::max(0.01f, m_CameraFovY / 60.0f) * 0.012f;
                            m_CameraPosition += (-right * delta.x + up * delta.y) * scale;
                        }
                    }

                    const float wheel = input->getMouseScrollDelta().y;
                    if (std::abs(wheel) > 0.0f)
                    {
                        m_FocusActive = false;
                        m_CameraPosition += makeForward(m_CameraYaw, m_CameraPitch) * (wheel * 0.45f);
                    }
                }
            }

            editorCamera                      = makeEditorCamera(m_CameraPosition,
                                            m_CameraYaw,
                                            m_CameraPitch,
                                            m_CameraFovY,
                                            aspect,
                                            renderTarget,
                                            editorSettings.rendererKey,
                                            editorSettings.clearMode,
                                            editorSettings.clearValue);
            editorCamera.selectionOutlineEnabled = Selection::lastId().valid();
            const auto updatedEditorCameraWorld = glm::inverse(editorCamera.view);
            ctx.state.sceneCamera.position    = glm::vec3(updatedEditorCameraWorld[3]);
            ctx.state.sceneCamera.rotation    = glm::normalize(glm::quat_cast(glm::inverse(editorCamera.view)));
            ctx.state.sceneCamera.fovYDegrees = m_CameraFovY;

            if (drawViewManipulator(imageMin, imageMax, editorCamera.view, editorCamera.projection))
            {
                applyViewMatrixToCamera(editorCamera.view, m_CameraPosition, m_CameraYaw, m_CameraPitch);
                editorCamera                      = makeEditorCamera(m_CameraPosition,
                                                m_CameraYaw,
                                                m_CameraPitch,
                                                m_CameraFovY,
                                                aspect,
                                                renderTarget,
                                                editorSettings.rendererKey,
                                                editorSettings.clearMode,
                                                editorSettings.clearValue);
                editorCamera.selectionOutlineEnabled = Selection::lastId().valid();
                const auto manipulatedEditorCameraWorld = glm::inverse(editorCamera.view);
                ctx.state.sceneCamera.position    = glm::vec3(manipulatedEditorCameraWorld[3]);
                ctx.state.sceneCamera.rotation    = glm::normalize(glm::quat_cast(glm::inverse(editorCamera.view)));
                ctx.state.sceneCamera.fovYDegrees = m_CameraFovY;
            }
        }
        else if (ui2DMode && sceneViewportHovered)
        {
            auto& io = ImGui::GetIO();
            if (ImGui::IsMouseDown(ImGuiMouseButton_Middle))
            {
                const ImVec2 delta = io.MouseDelta;
                if (delta.x != 0.0f || delta.y != 0.0f)
                {
                    m_Ui2DViewPanPx += glm::vec2 {delta.x, delta.y};
                    m_Ui2DDrag = {};
                    ImGui::SetNextFrameWantCaptureMouse(true);
                }
            }

            if (std::abs(io.MouseWheel) > 0.0f && m_Ui2DDrag.operation == Ui2DDragOperation::None)
            {
                const float oldZoom = std::clamp(m_Ui2DViewZoom, kUi2DZoomMin, kUi2DZoomMax);
                const float newZoom = std::clamp(oldZoom * std::pow(1.15f, io.MouseWheel), kUi2DZoomMin, kUi2DZoomMax);
                if (newZoom != oldZoom)
                {
                    const ImVec2 layoutMin = uiCanvasLayoutMin(imagePos);
                    const ImVec2 mouse     = io.MousePos;
                    const glm::vec2 focalFromLayout {mouse.x - layoutMin.x, mouse.y - layoutMin.y};
                    const float zoomRatio = newZoom / oldZoom;
                    m_Ui2DViewPanPx = focalFromLayout - (focalFromLayout - m_Ui2DViewPanPx) * zoomRatio;
                    m_Ui2DViewZoom  = newZoom;
                    ImGui::SetNextFrameWantCaptureMouse(true);
                }
            }
        }

        if (renderTarget != nullptr && ctx.services)
        {
            if (auto* cameraService = ctx.services->tryGet<vultra::ICameraService>())
            {
                if (ui2DMode)
                {
                    if (auto* worldService = ctx.services->tryGet<vultra::IWorldService>())
                    {
                        auto& world = worldService->world();
                        auto& reg   = world.registry();
                        std::optional<std::pair<glm::vec2, float>> previewTransform;
                        for (auto canvasEntity : reg.view<vultra::CanvasComponent>())
                        {
                            const auto& canvas = reg.get<vultra::CanvasComponent>(canvasEntity);
                            if (!canvas.enabled || !uiEntityVisible(reg, canvasEntity))
                                continue;
                            const glm::vec2 reference = glm::max(canvas.referenceResolutionPx, glm::vec2 {1.0f});
                            const float fit = uiCanvasViewportScale(canvas, avail, m_Ui2DViewZoom);
                            const ImVec2 canvasMin = uiCanvasMin(imagePos, avail, reference, fit, m_Ui2DViewPanPx);
                            previewTransform = std::make_pair(glm::vec2 {canvasMin.x - imagePos.x, canvasMin.y - imagePos.y}, fit);
                            break;
                        }
                        if (previewTransform)
                        {
                            editorCamera.uiOverlayTransformOverride = true;
                            editorCamera.uiOverlayOffsetPx          = previewTransform->first;
                            editorCamera.uiOverlayScale             = previewTransform->second;
                        }
                    }
                    applyEditorPlaybackTime(editorCamera);
                    cameraService->addManualCamera(editorCamera);
                }
                else
                {
                    applyEditorPlaybackTime(editorCamera);
                    cameraService->addManualCamera(editorCamera);
                    if (ctx.state.scenePicking.requested && supportsScenePicking(ctx))
                    {
                        ensurePickingRenderTarget(ctx, static_cast<uint32_t>(avail.x), static_cast<uint32_t>(avail.y));
                        if (m_PickingRenderTarget.texture)
                        {
                            auto pickingCamera                    = makeEditorCamera(m_CameraPosition,
                                                                  m_CameraYaw,
                                                                  m_CameraPitch,
                                                                  m_CameraFovY,
                                                                  aspect,
                                                                  &*m_PickingRenderTarget.texture,
                                                                  "universal",
                                                                  0u,
                                                                  glm::vec4 {0.035f, 0.04f, 0.052f, 1.0f});
                            pickingCamera.name                    = "Scene Picking";
                            pickingCamera.priority                = editorCamera.priority + 1;
                            pickingCamera.debugEntityIdOutput     = true;
                            pickingCamera.selectionOutlineEnabled = false;
                            applyEditorPlaybackTime(pickingCamera);
                            cameraService->addManualCamera(pickingCamera);
                        }
                    }
                }
            }
        }

        if (ctx.services)
        {
            if (auto* worldService = ctx.services->tryGet<vultra::IWorldService>(); m_ViewMode == ViewMode::Ui2D && worldService)
            {
                auto& world = worldService->world();
                auto& reg = world.registry();
                const auto mouse = ImGui::GetIO().MousePos;
                m_Ui2DViewZoom = std::clamp(m_Ui2DViewZoom, kUi2DZoomMin, kUi2DZoomMax);
                const auto hoveredHit = sceneViewportHovered ? hitTestUiCanvases(world,
                                                                                 imagePos,
                                                                                 avail,
                                                                                 m_Ui2DViewZoom,
                                                                                 m_Ui2DViewPanPx,
                                                                                 mouse) :
                                                               std::optional<UiEditorHit> {};
                if (sceneViewportHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                    m_Ui2DDrag.operation == Ui2DDragOperation::None)
                {
                    const auto selectedBefore = findEntityByUUID(world, Selection::lastId());
                    if (hoveredHit && hoveredHit->entity != selectedBefore)
                    {
                        if (auto* id = reg.try_get<vultra::IDComponent>(hoveredHit->entity))
                        {
                            ctx.state.selectedSourceAsset.clear();
                            Selection::select(SelectionCategory::Entity, id->uuid);
                        }
                    }
                    else if (!hoveredHit && m_Tool == Tool::Select)
                    {
                        Selection::clear(SelectionCategory::Entity);
                    }
                }

                const auto selectedEntity = findEntityByUUID(world, Selection::lastId());
                const auto hoveredEntity = hoveredHit ? hoveredHit->entity : entt::null;
                drawUiCanvasOverlay(ctx,
                                    world,
                                    imagePos,
                                    avail,
                                    selectedEntity,
                                    hoveredEntity,
                                    m_Tool,
                                    true,
                                    m_Ui2DViewZoom,
                                    m_Ui2DViewPanPx,
                                    m_ShowGrid,
                                    m_UiSnapEnabled,
                                    m_Ui2DDrag);
            }
            else if (m_Tool != Tool::Select)
            {
                if (auto* worldService = ctx.services->tryGet<vultra::IWorldService>())
                {
                    auto& world = worldService->world();
                    auto& reg   = world.registry();
                    auto  e     = findEntityByUUID(world, Selection::lastId());
                    if (e != entt::null && reg.valid(e) && reg.all_of<vultra::TransformComponent>(e))
                    {
                        auto& transform = reg.get<vultra::TransformComponent>(e);
                        auto  matrix    = makeWorldTransformMatrix(reg, e);

                        ImGuizmo::SetOrthographic(false);
                        ImGuizmo::SetDrawlist();
                        ImGuizmo::SetRect(imagePos.x, imagePos.y, avail.x, avail.y);
                        if (ImGuizmo::Manipulate(glm::value_ptr(editorCamera.view),
                                                 glm::value_ptr(editorCamera.projection),
                                                 toGizmoOperation(m_Tool),
                                                 m_CoordinateMode == CoordinateMode::Local ? ImGuizmo::LOCAL :
                                                                                              ImGuizmo::WORLD,
                                                 glm::value_ptr(matrix)))
                        {
                            if (setLocalTransformFromGizmoMatrix(reg, e, transform, matrix, m_Tool))
                            {
                                ctx.state.sceneDirty = true;
                                if (ctx.history)
                                    ctx.history->setNextLabel("Transform Entity");
                            }
                        }
                    }
                }
            }
        }

        drawGameViewOverlay(ctx, imageMin, imageMax);

        if (ctx.state.scenePicking.requested && sceneViewportHovered)
        {
            ctx.state.scenePicking.requested       = false;
            ctx.state.scenePicking.readbackPending = true;
        }
        else if (ctx.state.scenePicking.requested && !sceneViewportHovered)
        {
            ctx.state.scenePicking.requested = false;
        }

        ImGui::End();
    }

    bool SceneViewWindow::drawViewManipulator(const ImVec2&    viewportMin,
                                              const ImVec2&    viewportMax,
                                              glm::mat4&       view,
                                              const glm::mat4& projection)
    {
        const ImVec2 viewportSize {viewportMax.x - viewportMin.x, viewportMax.y - viewportMin.y};
        if (viewportSize.x < kViewManipulatorSize + kViewManipulatorMargin * 2.0f ||
            viewportSize.y < kViewManipulatorSize + kViewManipulatorMargin * 2.0f)
            return false;

        const ImVec2 position {viewportMax.x - kViewManipulatorSize - kViewManipulatorMargin,
                               viewportMin.y + kViewManipulatorMargin};
        const ImVec2 center {position.x + kViewManipulatorSize * 0.5f, position.y + kViewManipulatorSize * 0.5f};
        const float  radius = kViewManipulatorSize * 0.5f;

        auto* drawList = ImGui::GetWindowDrawList();
        drawList->AddCircleFilled(center, radius, IM_COL32(16, 19, 24, 128), 48);
        drawList->AddCircle(center, radius, IM_COL32(255, 255, 255, 32), 48, 1.0f);

        const glm::vec3 cameraPosition = m_CameraPosition;
        glm::mat4       gizmoView      = view;
        const glm::mat4 gizmoProjection {1.0f};

        ImOGuizmo::config.axisLengthScale = 0.30f;
        ImOGuizmo::SetRect(position.x, position.y, kViewManipulatorSize);
        ImOGuizmo::SetDrawList(drawList);
        bool changed = ImOGuizmo::DrawGizmo(glm::value_ptr(gizmoView), glm::value_ptr(gizmoProjection), 1.0f);

        if (changed)
        {
            const glm::mat4 invView = glm::inverse(gizmoView);
            const glm::quat rotation = glm::normalize(glm::quat_cast(invView));
            const glm::vec3 forward = glm::normalize(rotation * glm::vec3 {0.0f, 0.0f, -1.0f});
            const glm::vec3 up = glm::normalize(rotation * glm::vec3 {0.0f, 1.0f, 0.0f});
            view = glm::lookAt(cameraPosition, cameraPosition + forward, up);
            m_FocusActive = false;
        }

        const bool mouseInside = isMouseOverViewManipulator(viewportMin, viewportMax);
        if (mouseInside && !changed && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            m_ViewManipulatorDragActive = true;
            m_ViewManipulatorArcballVector = mapArcballPoint(ImGui::GetIO().MousePos, center, radius);
            ImGui::SetNextFrameWantCaptureMouse(true);
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            m_ViewManipulatorDragActive = false;

        if (m_ViewManipulatorDragActive && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
        {
            const glm::vec3 next = mapArcballPoint(ImGui::GetIO().MousePos, center, radius);
            const glm::quat delta = arcballDelta(m_ViewManipulatorArcballVector, next);
            m_ViewManipulatorArcballVector = next;

            const glm::mat4 invView = glm::inverse(view);
            const glm::quat cameraRotation = glm::normalize(glm::quat_cast(invView));
            const glm::quat worldDelta = cameraRotation * delta * glm::inverse(cameraRotation);
            const glm::quat nextRotation = glm::normalize(worldDelta * cameraRotation);
            const glm::vec3 nextForward = glm::normalize(nextRotation * glm::vec3 {0.0f, 0.0f, -1.0f});
            const glm::vec3 nextUp = glm::normalize(nextRotation * glm::vec3 {0.0f, 1.0f, 0.0f});

            view = glm::lookAt(cameraPosition, cameraPosition + nextForward, nextUp);
            m_FocusActive = false;
            changed = true;
            ImGui::SetNextFrameWantCaptureMouse(true);
        }

        return changed;
    }

    void SceneViewWindow::drawToolbar(EditorContext& ctx)
    {
        (void)ctx;
        constexpr float buttonSize = 28.0f;
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2 {0.0f, 0.0f});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2 {4.0f, 0.0f});

        auto toolButton = [&](const char* icon, const char* label, Tool tool) {
            const bool selected = m_Tool == tool;
            if (selected)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4 {0.12f, 0.35f, 0.72f, 0.95f});
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4 {0.16f, 0.43f, 0.86f, 1.0f});
            }
            const bool pressed = ImGui::Button(icon, ImVec2 {buttonSize, buttonSize});
            tooltip(label);
            if (selected)
                ImGui::PopStyleColor(2);
            if (pressed)
                m_Tool = tool;
        };

        toolButton(ICON_MDI_CURSOR_DEFAULT, "Select (Q)", Tool::Select);
        ImGui::SameLine();
        toolButton(ICON_MDI_AXIS_ARROW, "Move (W)", Tool::Move);
        ImGui::SameLine();
        toolButton(ICON_MDI_ROTATE_3D, "Rotate (E)", Tool::Rotate);
        ImGui::SameLine();
        toolButton(ICON_MDI_RESIZE, "Scale (R)", Tool::Scale);
        ImGui::SameLine();
        toolButton(ICON_MDI_RECTANGLE_OUTLINE, "Rect Tool (T)", Tool::Rect);
        ImGui::SameLine();
        toolButton(ICON_MDI_ARROW_ALL, "Transform Tool (Y)", Tool::Transform);
        ImGui::SameLine(0.0f, 10.0f);

        const bool local = m_CoordinateMode == CoordinateMode::Local;
        if (local)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4 {0.17f, 0.27f, 0.38f, 0.95f});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4 {0.20f, 0.34f, 0.48f, 1.0f});
        }
        if (ImGui::Button(ICON_MDI_AXIS_ARROW " Local", ImVec2 {76.0f, buttonSize}))
            m_CoordinateMode = CoordinateMode::Local;
        tooltip("Local coordinate space");
        if (local)
            ImGui::PopStyleColor(2);
        ImGui::SameLine();
        const bool global = m_CoordinateMode == CoordinateMode::Global;
        if (global)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4 {0.17f, 0.27f, 0.38f, 0.95f});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4 {0.20f, 0.34f, 0.48f, 1.0f});
        }
        if (ImGui::Button(ICON_MDI_EARTH " Global", ImVec2 {82.0f, buttonSize}))
            m_CoordinateMode = CoordinateMode::Global;
        tooltip("Global coordinate space");
        if (global)
            ImGui::PopStyleColor(2);
        ImGui::SameLine(0.0f, 10.0f);

        if (m_ShowGrid)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4 {0.17f, 0.38f, 0.28f, 0.95f});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4 {0.20f, 0.48f, 0.34f, 1.0f});
        }
        if (ImGui::Button(ICON_MDI_GRID, ImVec2 {buttonSize, buttonSize}))
            m_ShowGrid = !m_ShowGrid;
        tooltip("Grid");
        if (m_ShowGrid)
            ImGui::PopStyleColor(2);
        ImGui::SameLine();
        const bool snapActive = m_ViewMode == ViewMode::Ui2D && m_UiSnapEnabled;
        if (snapActive)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4 {0.42f, 0.30f, 0.12f, 0.95f});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4 {0.54f, 0.38f, 0.16f, 1.0f});
        }
        ImGui::BeginDisabled(m_ViewMode != ViewMode::Ui2D);
        if (ImGui::Button(m_UiSnapEnabled ? ICON_MDI_MAGNET_ON : ICON_MDI_MAGNET, ImVec2 {buttonSize, buttonSize}))
            m_UiSnapEnabled = !m_UiSnapEnabled;
        ImGui::EndDisabled();
        tooltip("UI Snap");
        if (snapActive)
            ImGui::PopStyleColor(2);
        ImGui::SameLine();
        if (m_ViewMode == ViewMode::Ui2D)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4 {0.42f, 0.30f, 0.12f, 0.95f});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4 {0.54f, 0.38f, 0.16f, 1.0f});
        }
        if (ImGui::Button(m_ViewMode == ViewMode::Ui2D ? "2D" : "3D", ImVec2 {buttonSize, buttonSize}))
            m_ViewMode = m_ViewMode == ViewMode::Ui2D ? ViewMode::View3D : ViewMode::Ui2D;
        tooltip("3D / 2D UI");
        if (m_ViewMode == ViewMode::Ui2D)
            ImGui::PopStyleColor(2);
        // Clear-color controls are 2D-only (they drive m_Ui2DClearColor). In 3D the scene
        // view follows the primary camera's clear mode, so don't draw them at all.
        if (m_ViewMode == ViewMode::Ui2D)
        {
            ImGui::SameLine();
            constexpr std::array<std::pair<const char*, glm::vec4>, 4> uiClearColorPresets {{
                {"##Ui2DClearColorBlack", {0.0f, 0.0f, 0.0f, 1.0f}},
                {"##Ui2DClearColorDark", {0.16f, 0.19f, 0.23f, 1.0f}},
                {"##Ui2DClearColorLight", {0.64f, 0.72f, 0.80f, 1.0f}},
                {"##Ui2DClearColorTransparentBlue", {0.10f, 0.16f, 0.24f, 1.0f}},
            }};
            for (const auto& [id, preset] : uiClearColorPresets)
            {
                if (ImGui::ColorButton(id,
                                       ImVec4 {preset.r, preset.g, preset.b, preset.a},
                                       ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_NoTooltip,
                                       ImVec2 {buttonSize, buttonSize}))
                    m_Ui2DClearColor = preset;
                tooltip("Apply UI Clear Color");
                ImGui::SameLine();
            }
            if (ImGui::Button(ICON_MDI_PALETTE, ImVec2 {buttonSize, buttonSize}))
                ImGui::OpenPopup("Ui2DClearColorPopup");
            tooltip("Custom UI Clear Color");
            if (ImGui::BeginPopup("Ui2DClearColorPopup"))
            {
                float color[3] {m_Ui2DClearColor.r, m_Ui2DClearColor.g, m_Ui2DClearColor.b};
                if (ImGui::ColorEdit3("Clear Color", color, ImGuiColorEditFlags_NoInputs))
                    m_Ui2DClearColor = glm::vec4 {color[0], color[1], color[2], 1.0f};
                ImGui::EndPopup();
            }
        }

        ImGui::PopStyleVar(3);
        ImGui::Separator();
    }

    void SceneViewWindow::drawGameViewOverlay(EditorContext& ctx, const ImVec2& viewportMin, const ImVec2& viewportMax)
    {
        if (ctx.state.gameViewVisibleLastFrame)
            return;

        const ImVec2 viewportSize {viewportMax.x - viewportMin.x, viewportMax.y - viewportMin.y};
        if (viewportSize.x < 220.0f || viewportSize.y < 160.0f)
            return;

        const float aspect    = 16.0f / 9.0f;
        m_GameOverlayZoom     = std::clamp(m_GameOverlayZoom, kOverlayZoomMin, kOverlayZoomMax);
        const float baseWidth = std::min(320.0f, std::max(180.0f, viewportSize.x * 0.22f));
        const float width     = std::min(viewportSize.x - 32.0f, baseWidth * m_GameOverlayZoom);
        const float height    = width / aspect;
        ensureGameOverlayRenderTarget(
            ctx, static_cast<uint32_t>(std::max(1.0f, width)), static_cast<uint32_t>(std::max(1.0f, height)));

        vultra::rhi::Texture* renderTarget =
            m_GameOverlayPendingRenderTarget.texture ? &*m_GameOverlayPendingRenderTarget.texture :
            m_GameOverlayActiveRenderTarget.texture  ? &*m_GameOverlayActiveRenderTarget.texture :
                                                       nullptr;

        bool hasPrimaryCamera = false;
        if (ctx.services && renderTarget)
        {
            if (auto* worldService = ctx.services->tryGet<vultra::IWorldService>())
            {
                auto& world      = worldService->world();
                auto  camera     = findPrimaryCamera(world);
                hasPrimaryCamera = camera != entt::null;
                if (hasPrimaryCamera)
                {
                    const bool simulationAdvancing =
                        ctx.state.editorPlaying && (!ctx.state.editorPaused || ctx.state.editorSteppingThisFrame);
                    const bool interactiveEdit = ImGui::IsAnyItemActive() || ImGui::IsMouseDown(ImGuiMouseButton_Left);
                    const auto signature =
                        gamePreviewSignature(ctx,
                                             world,
                                             camera,
                                             static_cast<uint32_t>(std::max(1.0f, width)),
                                             static_cast<uint32_t>(std::max(1.0f, height)));
                    const bool sceneDirtyEdge = ctx.state.sceneDirty != m_GameOverlayLastSceneDirty;
                    m_GameOverlayLastSceneDirty = ctx.state.sceneDirty;
                    const bool shouldRender = simulationAdvancing || interactiveEdit || !m_GameOverlayStaticFrameValid ||
                                              m_GameOverlayLastRenderSignature != signature || sceneDirtyEdge;
                    if (shouldRender)
                    {
                        m_GameOverlayStaticFrameValid    = true;
                        m_GameOverlayLastRenderSignature = simulationAdvancing || interactiveEdit ? 0 : signature;
                    }

                    if (shouldRender)
                    {
                        if (auto* cameraService = ctx.services->tryGet<vultra::ICameraService>())
                        {
                            auto previewCamera = makeGameOverlayCamera(world, camera, aspect, renderTarget);
                            previewCamera.overrideFrameTime = true;
                            previewCamera.frameTimeSeconds =
                                ctx.state.editorPlaying ? ctx.state.editorGameTimeSeconds : 0.0f;
                            previewCamera.frameDeltaSeconds =
                                ctx.state.editorPlaying ? ctx.state.editorGameDeltaSeconds : 0.0f;
                            previewCamera.worldOverride = &world;
                            cameraService->addManualCamera(previewCamera);
                        }
                    }
                }
            }
        }

        const ImVec2    padding {14.0f, 14.0f};
        constexpr float controlHeight = 30.0f;
        const ImVec2    panelSize {width + padding.x * 2.0f, height + padding.y * 2.0f + 22.0f + controlHeight};
        const ImVec2    panelMin {viewportMin.x + 16.0f, viewportMax.y - panelSize.y - 16.0f};
        const ImVec2    panelMax {panelMin.x + panelSize.x, panelMin.y + panelSize.y};
        const ImVec2    imageMin {panelMin.x + padding.x, panelMin.y + padding.y + 22.0f};
        const ImVec2    imageMax {imageMin.x + width, imageMin.y + height};
        const ImVec2    controlsMin {imageMin.x, imageMax.y + 8.0f};
        const ImVec2    mouse    = ImGui::GetIO().MousePos;
        const auto      contains = [&](const ImVec2& min, const ImVec2& max) {
            return mouse.x >= min.x && mouse.x <= max.x && mouse.y >= min.y && mouse.y <= max.y;
        };
        const ImVec2 minusMin {controlsMin.x, controlsMin.y};
        const ImVec2 minusMax {minusMin.x + 24.0f, minusMin.y + 24.0f};
        const ImVec2 labelMin {minusMax.x + 10.0f, controlsMin.y + 4.0f};
        const ImVec2 plusMin {labelMin.x + 56.0f, controlsMin.y};
        const ImVec2 plusMax {plusMin.x + 24.0f, plusMin.y + 24.0f};

        const bool minusHovered = contains(minusMin, minusMax);
        const bool plusHovered  = contains(plusMin, plusMax);
        if ((minusHovered || plusHovered) && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            if (minusHovered)
                m_GameOverlayZoom = std::clamp(m_GameOverlayZoom - kOverlayZoomStep, kOverlayZoomMin, kOverlayZoomMax);
            else
                m_GameOverlayZoom = std::clamp(m_GameOverlayZoom + kOverlayZoomStep, kOverlayZoomMin, kOverlayZoomMax);
            ImGui::SetNextFrameWantCaptureMouse(true);
        }

        char zoomLabel[16] {};
        std::snprintf(zoomLabel, sizeof(zoomLabel), "%.0f%%", m_GameOverlayZoom * 100.0f);

        auto* drawList = ImGui::GetWindowDrawList();
        drawList->PushClipRect(viewportMin, viewportMax, true);
        drawList->AddRectFilled(panelMin, panelMax, IM_COL32(10, 14, 18, 255), 7.0f);
        drawList->AddRect(panelMin, panelMax, IM_COL32(68, 86, 105, 255), 7.0f);
        drawList->AddText(ImVec2(panelMin.x + padding.x, panelMin.y + 8.0f), IM_COL32(190, 204, 218, 255), "Game View");
        const ImVec2 zoomSize = ImGui::CalcTextSize(zoomLabel);
        drawList->AddText(
            ImVec2(panelMax.x - padding.x - zoomSize.x, panelMin.y + 8.0f), IM_COL32(126, 142, 158, 255), zoomLabel);

        if (m_GameOverlayActiveRenderTarget.textureId && hasPrimaryCamera)
        {
            drawList->AddRectFilled(imageMin, imageMax, IM_COL32(0, 0, 0, 255), 3.0f);
            drawList->AddImage(
                m_GameOverlayActiveRenderTarget.textureId, imageMin, imageMax, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f));
        }
        else
        {
            drawList->AddRectFilled(imageMin, imageMax, IM_COL32(16, 19, 24, 255), 3.0f);
            const char*  label    = hasPrimaryCamera ? "Preparing preview" : "No primary camera";
            const ImVec2 textSize = ImGui::CalcTextSize(label);
            drawList->AddText(
                ImVec2((imageMin.x + imageMax.x - textSize.x) * 0.5f, (imageMin.y + imageMax.y - textSize.y) * 0.5f),
                IM_COL32(140, 152, 166, 255),
                label);
        }
        drawList->AddRect(imageMin, imageMax, IM_COL32(72, 86, 104, 255), 3.0f);
        const auto buttonColor = [](bool hovered) {
            return hovered ? IM_COL32(42, 50, 62, 255) : IM_COL32(26, 31, 39, 255);
        };
        drawList->AddRectFilled(minusMin, minusMax, buttonColor(minusHovered), 5.0f);
        drawList->AddText(
            ImVec2(minusMin.x + 4.0f, minusMin.y + 4.0f), IM_COL32(184, 198, 214, 255), ICON_MDI_MAGNIFY_MINUS);
        drawList->AddText(labelMin, IM_COL32(126, 142, 158, 255), zoomLabel);
        drawList->AddRectFilled(plusMin, plusMax, buttonColor(plusHovered), 5.0f);
        drawList->AddText(
            ImVec2(plusMin.x + 4.0f, plusMin.y + 4.0f), IM_COL32(184, 198, 214, 255), ICON_MDI_MAGNIFY_PLUS);
        drawList->PopClipRect();
    }

    void SceneViewWindow::ensureRenderTarget(EditorContext& ctx, const uint32_t width, const uint32_t height)
    {
        if (!ctx.services)
            return;
        if (width == 0u || height == 0u)
            return;

        collectRetiredRenderTargets(ctx);
        if (m_PendingRenderTarget.texture &&
            static_cast<uint64_t>(ImGui::GetFrameCount()) > m_PendingRenderTarget.frameCreated)
        {
            promotePendingRenderTarget(ctx);
        }

        const auto& currentTarget = m_PendingRenderTarget.texture ? m_PendingRenderTarget : m_ActiveRenderTarget;
        if (currentTarget.texture && currentTarget.extent.width == width && currentTarget.extent.height == height &&
            currentTarget.textureId)
        {
            m_RenderTargetResizeRequest = {};
            return;
        }

        const auto frame = static_cast<uint64_t>(ImGui::GetFrameCount());
        if (currentTarget.texture)
        {
            if (m_RenderTargetResizeRequest.extent.width != width ||
                m_RenderTargetResizeRequest.extent.height != height)
            {
                m_RenderTargetResizeRequest.extent         = {width, height};
                m_RenderTargetResizeRequest.firstSeenFrame = frame;
                return;
            }

            if (frame < m_RenderTargetResizeRequest.firstSeenFrame + kRenderTargetResizeStableFrames)
                return;
        }

        if (m_PendingRenderTarget.texture)
            retireRenderTarget(m_PendingRenderTarget);

        auto* backendService = ctx.services->tryGet<vultra::IRenderBackendService>();
        auto* imguiService   = ctx.services->tryGet<vultra::IImGuiService>();
        if (!backendService || !imguiService)
            return;

        auto& rd     = backendService->renderDevice();
        auto  format = vultra::rhi::PixelFormat::eRGBA8_UNorm;

        m_PendingRenderTarget.extent = {width, height};
        m_PendingRenderTarget.texture =
            vultra::rhi::Texture::Builder {}
                .setExtent(m_PendingRenderTarget.extent)
                .setPixelFormat(format)
                .setNumMipLevels(1)
                .setUsageFlags(vultra::rhi::ImageUsage::eRenderTarget | vultra::rhi::ImageUsage::eSampled |
                               vultra::rhi::ImageUsage::eTransferSrc)
                .build(rd);
        m_PendingRenderTarget.textureId    = imguiService->addTexture(*m_PendingRenderTarget.texture);
        m_PendingRenderTarget.frameCreated = static_cast<uint64_t>(ImGui::GetFrameCount());
        m_PendingRenderTarget.releaseFrame = 0;
        m_RenderTargetResizeRequest        = {};
    }

    void SceneViewWindow::ensurePickingRenderTarget(EditorContext& ctx, const uint32_t width, const uint32_t height)
    {
        if (!ctx.services || width == 0u || height == 0u)
            return;

        collectRetiredPickingRenderTargets();
        if (m_PickingRenderTarget.texture && m_PickingRenderTarget.extent.width == width &&
            m_PickingRenderTarget.extent.height == height)
        {
            return;
        }

        retirePickingRenderTarget(m_PickingRenderTarget);

        auto* backendService = ctx.services->tryGet<vultra::IRenderBackendService>();
        if (!backendService)
            return;

        auto& rd                     = backendService->renderDevice();
        m_PickingRenderTarget.extent = {width, height};
        m_PickingRenderTarget.texture =
            vultra::rhi::Texture::Builder {}
                .setExtent(m_PickingRenderTarget.extent)
                .setPixelFormat(vultra::rhi::PixelFormat::eRGBA8_UNorm)
                .setNumMipLevels(1)
                .setUsageFlags(vultra::rhi::ImageUsage::eRenderTarget | vultra::rhi::ImageUsage::eTransferSrc)
                .build(rd);
        m_PickingRenderTarget.frameCreated = static_cast<uint64_t>(ImGui::GetFrameCount());
        m_PickingRenderTarget.releaseFrame = 0;
    }

    void SceneViewWindow::ensureGameOverlayRenderTarget(EditorContext& ctx, const uint32_t width, const uint32_t height)
    {
        if (!ctx.services || width == 0u || height == 0u)
            return;

        collectRetiredGameOverlayRenderTargets(ctx);
        if (m_GameOverlayPendingRenderTarget.texture &&
            static_cast<uint64_t>(ImGui::GetFrameCount()) > m_GameOverlayPendingRenderTarget.frameCreated)
        {
            promotePendingGameOverlayRenderTarget(ctx);
        }

        const auto& currentTarget = m_GameOverlayPendingRenderTarget.texture ? m_GameOverlayPendingRenderTarget :
                                                                               m_GameOverlayActiveRenderTarget;
        if (currentTarget.texture && currentTarget.extent.width == width && currentTarget.extent.height == height &&
            currentTarget.textureId)
            return;

        if (m_GameOverlayPendingRenderTarget.texture)
            retireGameOverlayRenderTarget(m_GameOverlayPendingRenderTarget);

        auto* backendService = ctx.services->tryGet<vultra::IRenderBackendService>();
        auto* imguiService   = ctx.services->tryGet<vultra::IImGuiService>();
        if (!backendService || !imguiService)
            return;

        auto& rd     = backendService->renderDevice();
        auto  format = backendService->backbuffer().getPixelFormat();
        if (format == vultra::rhi::PixelFormat::eUndefined)
            format = vultra::rhi::PixelFormat::eRGBA8_UNorm;

        m_GameOverlayPendingRenderTarget.extent = {width, height};
        m_GameOverlayPendingRenderTarget.texture =
            vultra::rhi::Texture::Builder {}
                .setExtent(m_GameOverlayPendingRenderTarget.extent)
                .setPixelFormat(format)
                .setNumMipLevels(1)
                .setUsageFlags(vultra::rhi::ImageUsage::eRenderTarget | vultra::rhi::ImageUsage::eSampled)
                .build(rd);
        m_GameOverlayPendingRenderTarget.textureId =
            imguiService->addTexture(*m_GameOverlayPendingRenderTarget.texture);
        m_GameOverlayPendingRenderTarget.frameCreated = static_cast<uint64_t>(ImGui::GetFrameCount());
        m_GameOverlayPendingRenderTarget.releaseFrame = 0;
    }

    void SceneViewWindow::promotePendingRenderTarget(EditorContext& ctx)
    {
        (void)ctx;
        if (!m_PendingRenderTarget.texture)
            return;

        retireRenderTarget(m_ActiveRenderTarget);
        m_ActiveRenderTarget  = std::move(m_PendingRenderTarget);
        m_PendingRenderTarget = {};
    }

    void SceneViewWindow::promotePendingGameOverlayRenderTarget(EditorContext& ctx)
    {
        (void)ctx;
        if (!m_GameOverlayPendingRenderTarget.texture)
            return;

        retireGameOverlayRenderTarget(m_GameOverlayActiveRenderTarget);
        m_GameOverlayActiveRenderTarget  = std::move(m_GameOverlayPendingRenderTarget);
        m_GameOverlayPendingRenderTarget = {};
    }

    void SceneViewWindow::retireRenderTarget(RenderTargetSlot& slot)
    {
        if (!slot.texture && !slot.textureId)
            return;

        slot.releaseFrame = static_cast<uint64_t>(ImGui::GetFrameCount()) + kRenderTargetReleaseDelayFrames;
        m_RetiredRenderTargets.push_back(std::move(slot));
        slot = {};
    }

    void SceneViewWindow::retirePickingRenderTarget(RenderTargetSlot& slot)
    {
        if (!slot.texture)
            return;

        slot.releaseFrame = static_cast<uint64_t>(ImGui::GetFrameCount()) + kRenderTargetReleaseDelayFrames;
        m_RetiredPickingRenderTargets.push_back(std::move(slot));
        slot = {};
    }

    void SceneViewWindow::retireGameOverlayRenderTarget(RenderTargetSlot& slot)
    {
        if (!slot.texture && !slot.textureId)
            return;

        slot.releaseFrame = static_cast<uint64_t>(ImGui::GetFrameCount()) + kRenderTargetReleaseDelayFrames;
        m_GameOverlayRetiredRenderTargets.push_back(std::move(slot));
        slot = {};
    }

    void SceneViewWindow::collectRetiredRenderTargets(EditorContext& ctx)
    {
        const auto frame        = static_cast<uint64_t>(ImGui::GetFrameCount());
        auto*      imguiService = ctx.services ? ctx.services->tryGet<vultra::IImGuiService>() : nullptr;

        std::size_t out = 0;
        for (auto& slot : m_RetiredRenderTargets)
        {
            if (frame >= slot.releaseFrame)
            {
                if (imguiService && slot.textureId)
                    imguiService->removeTexture(slot.textureId);
                slot.texture.reset();
            }
            else
            {
                m_RetiredRenderTargets[out++] = std::move(slot);
            }
        }
        m_RetiredRenderTargets.resize(out);
    }

    void SceneViewWindow::collectRetiredPickingRenderTargets()
    {
        const auto frame = static_cast<uint64_t>(ImGui::GetFrameCount());

        std::size_t out = 0;
        for (auto& slot : m_RetiredPickingRenderTargets)
        {
            if (frame >= slot.releaseFrame)
            {
                slot.texture.reset();
            }
            else
            {
                m_RetiredPickingRenderTargets[out++] = std::move(slot);
            }
        }
        m_RetiredPickingRenderTargets.resize(out);
    }

    void SceneViewWindow::collectRetiredGameOverlayRenderTargets(EditorContext& ctx)
    {
        const auto frame        = static_cast<uint64_t>(ImGui::GetFrameCount());
        auto*      imguiService = ctx.services ? ctx.services->tryGet<vultra::IImGuiService>() : nullptr;

        std::size_t out = 0;
        for (auto& slot : m_GameOverlayRetiredRenderTargets)
        {
            if (frame >= slot.releaseFrame)
            {
                if (imguiService && slot.textureId)
                    imguiService->removeTexture(slot.textureId);
                slot.texture.reset();
            }
            else
            {
                m_GameOverlayRetiredRenderTargets[out++] = std::move(slot);
            }
        }
        m_GameOverlayRetiredRenderTargets.resize(out);
    }

    void SceneViewWindow::releaseRenderTarget(EditorContext& ctx)
    {
        const bool hasOwnedRenderTargets = m_ActiveRenderTarget.texture || m_PendingRenderTarget.texture ||
                                           !m_RetiredRenderTargets.empty();
        if (hasOwnedRenderTargets)
        {
            if (auto* backendService = ctx.services ? ctx.services->tryGet<vultra::IRenderBackendService>() : nullptr)
                backendService->renderDevice().waitIdle();
        }
        if (ctx.services)
        {
            if (auto* imguiService = ctx.services->tryGet<vultra::IImGuiService>())
            {
                if (m_ActiveRenderTarget.textureId)
                    imguiService->removeTexture(m_ActiveRenderTarget.textureId);
                if (m_PendingRenderTarget.textureId)
                    imguiService->removeTexture(m_PendingRenderTarget.textureId);
                for (auto& slot : m_RetiredRenderTargets)
                {
                    if (slot.textureId)
                        imguiService->removeTexture(slot.textureId);
                }
            }
        }
        m_ActiveRenderTarget  = {};
        m_PendingRenderTarget = {};
        m_RetiredRenderTargets.clear();
        m_RenderTargetResizeRequest = {};
    }

    void SceneViewWindow::releasePickingRenderTarget(EditorContext& ctx)
    {
        if (m_PickingRenderTarget.texture || !m_RetiredPickingRenderTargets.empty())
        {
            if (auto* backendService = ctx.services ? ctx.services->tryGet<vultra::IRenderBackendService>() : nullptr)
                backendService->renderDevice().waitIdle();
        }
        m_PickingRenderTarget = {};
        m_RetiredPickingRenderTargets.clear();
    }

    void SceneViewWindow::releaseGameOverlayRenderTarget(EditorContext& ctx)
    {
        const bool hasOwnedRenderTargets = m_GameOverlayActiveRenderTarget.texture ||
                                           m_GameOverlayPendingRenderTarget.texture ||
                                           !m_GameOverlayRetiredRenderTargets.empty();
        if (hasOwnedRenderTargets)
        {
            if (auto* backendService = ctx.services ? ctx.services->tryGet<vultra::IRenderBackendService>() : nullptr)
                backendService->renderDevice().waitIdle();
        }
        if (ctx.services)
        {
            if (auto* renderService = ctx.services->tryGet<vultra::IRenderService>())
            {
                if (auto* worldService = ctx.services->tryGet<vultra::IWorldService>())
                    renderService->releaseOverrideRenderWorld(&worldService->world());
            }
            if (auto* imguiService = ctx.services->tryGet<vultra::IImGuiService>())
            {
                if (m_GameOverlayActiveRenderTarget.textureId)
                    imguiService->removeTexture(m_GameOverlayActiveRenderTarget.textureId);
                if (m_GameOverlayPendingRenderTarget.textureId)
                    imguiService->removeTexture(m_GameOverlayPendingRenderTarget.textureId);
                for (auto& slot : m_GameOverlayRetiredRenderTargets)
                {
                    if (slot.textureId)
                        imguiService->removeTexture(slot.textureId);
                }
            }
        }
        m_GameOverlayActiveRenderTarget  = {};
        m_GameOverlayPendingRenderTarget = {};
        m_GameOverlayRetiredRenderTargets.clear();
        m_GameOverlayStaticFrameValid    = false;
        m_GameOverlayLastRenderSignature = 0;
    }

    void SceneViewWindow::resetRenderTargetsForProject(EditorContext& ctx)
    {
        if (m_ProjectGeneration == ctx.state.projectGeneration)
            return;

        retireRenderTarget(m_ActiveRenderTarget);
        retireRenderTarget(m_PendingRenderTarget);
        retirePickingRenderTarget(m_PickingRenderTarget);
        retireGameOverlayRenderTarget(m_GameOverlayActiveRenderTarget);
        retireGameOverlayRenderTarget(m_GameOverlayPendingRenderTarget);
        if (ctx.services)
        {
            if (auto* renderService = ctx.services->tryGet<vultra::IRenderService>())
            {
                if (auto* worldService = ctx.services->tryGet<vultra::IWorldService>())
                    renderService->releaseOverrideRenderWorld(&worldService->world());
            }
        }
        m_GameOverlayStaticFrameValid    = false;
        m_GameOverlayLastRenderSignature = 0;
        m_RenderTargetResizeRequest      = {};
        m_ProjectGeneration              = ctx.state.projectGeneration;
        m_CameraInitializedFromScene     = false;
    }

    void SceneViewWindow::initializeCameraFromPrimaryCamera(EditorContext& ctx)
    {
        if (m_CameraInitializedFromScene)
            return;

        auto* worldService = ctx.services ? ctx.services->tryGet<vultra::IWorldService>() : nullptr;
        if (!worldService)
            return;

        auto&      world  = worldService->world();
        auto&      reg    = world.registry();
        const auto entity = findPrimaryCamera(world);
        if (entity == entt::null || !reg.valid(entity) ||
            !reg.all_of<vultra::TransformComponent, vultra::CameraComponent>(entity))
            return;

        const auto& camera         = reg.get<vultra::CameraComponent>(entity);
        const auto  worldTransform = makeWorldTransformMatrix(reg, entity);
        const auto  rotation       = glm::normalize(glm::quat_cast(worldTransform));

        m_CameraPosition = glm::vec3(worldTransform[3]);
        if (camera.projection == 0u)
            m_CameraFovY = camera.fovYDegrees;

        const auto forward           = glm::normalize(rotation * glm::vec3 {0.0f, 0.0f, -1.0f});
        m_CameraYaw                  = glm::degrees(std::atan2(forward.z, forward.x));
        m_CameraPitch                = glm::degrees(std::asin(std::clamp(forward.y, -1.0f, 1.0f)));
        m_CameraInitializedFromScene = true;
    }
} // namespace vultra_app
