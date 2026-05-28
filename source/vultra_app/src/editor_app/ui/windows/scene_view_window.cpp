#include "editor_app/ui/windows/scene_view_window.hpp"

#include "editor_app/editor_history.hpp"
#include "editor_app/selection.hpp"

#include <IconsMaterialDesignIcons.h>
#include <vultra/core/services/input_service.hpp>
#include <vultra/function/rendering/render_structs.hpp>
#include <vultra/function/services/asset_service.hpp>
#include <vultra/function/services/camera_service.hpp>
#include <vultra/function/services/render_backend_service.hpp>
#include <vultra/function/services/world_service.hpp>
#include <vultra/function/world/components/camera_component.hpp>
#include <vultra/function/world/components/hierarchy_component.hpp>
#include <vultra/function/world/components/id_component.hpp>
#include <vultra/function/world/components/mesh_component.hpp>
#include <vultra/function/world/components/name_component.hpp>
#include <vultra/function/world/components/transform_component.hpp>
#include <vultra/function/world/world.hpp>

#include <ImGuizmo/ImGuizmo.h>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <imoguizmo/imoguizmo.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>

namespace vultra_app
{
    namespace
    {
        constexpr uint64_t  kRenderTargetReleaseDelayFrames = 3;
        constexpr float     kOverlayZoomMin                 = 0.5f;
        constexpr float     kOverlayZoomMax                 = 4.0f;
        constexpr float     kOverlayZoomStep                = 0.25f;
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

                auto mesh = assets.loadMeshSync(meshComponent.mesh);
                if (!mesh.ready() || !mesh.cpu())
                    continue;

                const auto worldMatrix = makeWorldTransformMatrix(reg, e);
                for (const auto& p : mesh.cpu()->positions)
                    bounds.include(glm::vec3(worldMatrix * glm::vec4(glm::vec3 {p.x, p.y, p.z}, 1.0f)));
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

        bool isMouseInRect(const ImVec2& min, const ImVec2& max)
        {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            return mouse.x >= min.x && mouse.x <= max.x && mouse.y >= min.y && mouse.y <= max.y;
        }

        bool isMouseOverToolbar(const ImVec2& viewportMin)
        {
            constexpr float buttonSize = 28.0f;
            constexpr float padding    = 6.0f;
            constexpr float gap        = 4.0f;
            constexpr int   itemCount  = 5;
            const ImVec2    panelPos {viewportMin.x + 12.0f, viewportMin.y + 12.0f};
            const ImVec2    buttonsMin {panelPos.x + padding, panelPos.y + padding};
            for (int i = 0; i < itemCount; ++i)
            {
                const float x = buttonsMin.x + static_cast<float>(i) * (buttonSize + gap);
                if (isMouseInRect(ImVec2 {x, buttonsMin.y}, ImVec2 {x + buttonSize, buttonsMin.y + buttonSize}))
                    return true;
            }
            return false;
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
            const float     width     = std::min(viewportSize.x - 32.0f,
                                         baseWidth * std::clamp(gameOverlayZoom, kOverlayZoomMin, kOverlayZoomMax));
            const float     height    = width / aspect;
            const ImVec2    padding {14.0f, 14.0f};
            constexpr float controlHeight = 30.0f;
            const ImVec2    panelSize {width + padding.x * 2.0f, height + padding.y * 2.0f + 22.0f + controlHeight};
            const ImVec2    panelMin {viewportMin.x + 16.0f, viewportMax.y - panelSize.y - 16.0f};
            const ImVec2    panelMax {panelMin.x + panelSize.x, panelMin.y + panelSize.y};
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
        releasePickingRenderTarget();
        releaseGameOverlayRenderTarget(ctx);
    }

    void SceneViewWindow::onDestroy(EditorContext& ctx)
    {
        releaseRenderTarget(ctx);
        releasePickingRenderTarget();
        releaseGameOverlayRenderTarget(ctx);
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
        if (!visible)
        {
            ImGui::End();
            return;
        }

        ImVec2 avail = ImGui::GetContentRegionAvail();
        avail.x      = std::max(1.0f, avail.x);
        avail.y      = std::max(1.0f, avail.y);

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

        if (m_ShowGrid)
        {
            constexpr float step = 32.0f;
            for (float x = imageMin.x; x < imageMax.x; x += step)
                dl->AddLine(ImVec2(x, imageMin.y), ImVec2(x, imageMax.y), IM_COL32(255, 255, 255, 18));
            for (float y = imageMin.y; y < imageMax.y; y += step)
                dl->AddLine(ImVec2(imageMin.x, y), ImVec2(imageMax.x, y), IM_COL32(255, 255, 255, 18));
        }
        dl->AddRect(imageMin, imageMax, IM_COL32(90, 100, 118, 255));
        drawToolbar(imageMin);

        const float aspect                = avail.x / std::max(avail.y, 1.0f);
        auto*       renderTarget          = m_PendingRenderTarget.texture ?
                                                &*m_PendingRenderTarget.texture :
                                                (m_ActiveRenderTarget.texture ? &*m_ActiveRenderTarget.texture : nullptr);
        const auto  editorSettings        = editorCameraSceneSettings(ctx);
        auto        editorCamera          = makeEditorCamera(m_CameraPosition,
                                             m_CameraYaw,
                                             m_CameraPitch,
                                             m_CameraFovY,
                                             aspect,
                                             renderTarget,
                                             editorSettings.rendererKey,
                                             editorSettings.clearMode,
                                             editorSettings.clearValue);
        ctx.state.sceneCamera.valid       = true;
        ctx.state.sceneCamera.position    = m_CameraPosition;
        ctx.state.sceneCamera.rotation    = glm::normalize(glm::quat_cast(glm::inverse(editorCamera.view)));
        ctx.state.sceneCamera.fovYDegrees = m_CameraFovY;

        const bool mouseOverToolbar         = isMouseOverToolbar(imageMin);
        const bool mouseOverViewManipulator = isMouseOverViewManipulator(imageMin, imageMax);
        const bool mouseOverGameOverlay     = isMouseOverGameOverlay(ctx, imageMin, imageMax, m_GameOverlayZoom);
        const bool sceneViewportHovered =
            hovered && !mouseOverToolbar && !mouseOverViewManipulator && !mouseOverGameOverlay;
        const bool flyActive = sceneViewportHovered && ImGui::IsMouseDown(ImGuiMouseButton_Right);
        if (sceneViewportHovered && !flyActive && !ImGui::GetIO().WantTextInput && !ImGuizmo::IsUsing())
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Q))
                m_Tool = Tool::Select;
            if (ImGui::IsKeyPressed(ImGuiKey_W))
                m_Tool = Tool::Move;
            if (ImGui::IsKeyPressed(ImGuiKey_E))
                m_Tool = Tool::Rotate;
            if (ImGui::IsKeyPressed(ImGuiKey_R))
                m_Tool = Tool::Scale;
            if (ImGui::IsKeyPressed(ImGuiKey_F))
                focusSelection(ctx, avail.x / std::max(avail.y, 1.0f));
        }

        if (ctx.services)
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
            ctx.state.sceneCamera.position    = m_CameraPosition;
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
                ctx.state.sceneCamera.position    = m_CameraPosition;
                ctx.state.sceneCamera.rotation    = glm::normalize(glm::quat_cast(glm::inverse(editorCamera.view)));
                ctx.state.sceneCamera.fovYDegrees = m_CameraFovY;
            }

            if (renderTarget != nullptr)
            {
                if (auto* cameraService = ctx.services->tryGet<vultra::ICameraService>())
                {
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
                            cameraService->addManualCamera(pickingCamera);
                        }
                    }
                }
            }

            if (m_Tool != Tool::Select)
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
                                                 ImGuizmo::LOCAL,
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

        ImOGuizmo::config.axisLengthScale = 0.30f;
        ImOGuizmo::SetRect(position.x, position.y, kViewManipulatorSize);
        ImOGuizmo::SetDrawList(drawList);

        const float pivotDistance = std::max(glm::length(m_CameraPosition), 0.001f);
        return ImOGuizmo::DrawGizmo(glm::value_ptr(view), glm::value_ptr(projection), pivotDistance);
    }

    void SceneViewWindow::drawToolbar(const ImVec2& viewportMin)
    {
        const ImVec2    panelPos {viewportMin.x + 12.0f, viewportMin.y + 12.0f};
        constexpr float buttonSize = 28.0f;
        constexpr float padding    = 6.0f;
        constexpr float gap        = 4.0f;
        constexpr int   itemCount  = 5;
        const ImVec2    panelSize {padding * 2.0f + buttonSize * itemCount + gap * (itemCount - 1), 40.0f};

        auto* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(
            panelPos, ImVec2 {panelPos.x + panelSize.x, panelPos.y + panelSize.y}, IM_COL32(20, 23, 28, 218), 10.0f);
        drawList->AddRect(ImVec2 {panelPos.x + 0.5f, panelPos.y + 0.5f},
                          ImVec2 {panelPos.x + panelSize.x - 0.5f, panelPos.y + panelSize.y - 0.5f},
                          IM_COL32(255, 255, 255, 32),
                          10.0f);

        ImGui::SetCursorScreenPos(ImVec2 {panelPos.x + padding, panelPos.y + padding});
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2 {0.0f, 0.0f});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2 {gap, 0.0f});

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

        ImGui::PopStyleVar(3);
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
                    if (auto* cameraService = ctx.services->tryGet<vultra::ICameraService>())
                        cameraService->addManualCamera(makeGameOverlayCamera(world, camera, aspect, renderTarget));
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
            return;

        if (m_PendingRenderTarget.texture)
            retireRenderTarget(m_PendingRenderTarget);

        auto* backendService = ctx.services->tryGet<vultra::IRenderBackendService>();
        auto* imguiService   = ctx.services->tryGet<vultra::IImGuiService>();
        if (!backendService || !imguiService)
            return;

        auto& rd     = backendService->renderDevice();
        auto  format = backendService->backbuffer().getPixelFormat();
        if (format == vultra::rhi::PixelFormat::eUndefined)
            format = vultra::rhi::PixelFormat::eRGBA8_UNorm;

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
    }

    void SceneViewWindow::releasePickingRenderTarget()
    {
        m_PickingRenderTarget = {};
        m_RetiredPickingRenderTargets.clear();
    }

    void SceneViewWindow::releaseGameOverlayRenderTarget(EditorContext& ctx)
    {
        if (ctx.services)
        {
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
        m_ProjectGeneration          = ctx.state.projectGeneration;
        m_CameraInitializedFromScene = false;
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
