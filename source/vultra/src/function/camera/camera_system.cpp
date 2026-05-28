#include "vultra/function/camera/camera_system.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/engine/engine_context.hpp"
#include "vultra/core/math/math.hpp"
#include "vultra/core/services/input_service.hpp"
#include "vultra/core/services/window_service.hpp"
#include "vultra/function/services/render_backend_service.hpp"
#include "vultra/function/services/world_service.hpp"
#include "vultra/function/world/components/camera_component.hpp"
#include "vultra/function/world/components/entity_status_component.hpp"
#include "vultra/function/world/components/hierarchy_component.hpp"
#include "vultra/function/world/components/id_component.hpp"
#include "vultra/function/world/components/name_component.hpp"
#include "vultra/function/world/components/transform_component.hpp"
#include "vultra/function/world/components/xr_view_component.hpp"
#include "vultra/function/world/world.hpp"

#include <texture_headers/kenney_cursor-pack/PNG/Outline/Default/hand_closed.png.bintex.h>
#include <texture_headers/kenney_cursor-pack/PNG/Outline/Default/look_b.png.bintex.h>
#include <texture_headers/kenney_cursor-pack/PNG/Outline/Default/pointer_a.png.bintex.h>
#include <texture_headers/kenney_cursor-pack/PNG/Outline/Default/rotate_cw.png.bintex.h>
#include <texture_headers/kenney_cursor-pack/PNG/Outline/Default/zoom_in.png.bintex.h>
#include <texture_headers/kenney_cursor-pack/PNG/Outline/Default/zoom_out.png.bintex.h>

#include <glm/common.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>

namespace vultra
{
    namespace
    {
        constexpr glm::vec3 kWorldUp {0.0f, 1.0f, 0.0f};

        const os::Window::CursorImage* orbitCursorImage()
        {
            static const auto cursor = os::Window::decodeCursorImage(rotate_cw_png_bintex, 8, 8);
            return cursor ? &(*cursor) : nullptr;
        }

        const os::Window::CursorImage* arrowCursorImage()
        {
            static const auto cursor = os::Window::decodeCursorImage(pointer_a_png_bintex, 8, 8);
            return cursor ? &(*cursor) : nullptr;
        }

        const os::Window::CursorImage* grabCursorImage()
        {
            static const auto cursor = os::Window::decodeCursorImage(hand_closed_png_bintex, 8, 8);
            return cursor ? &(*cursor) : nullptr;
        }

        const os::Window::CursorImage* lookCursorImage()
        {
            static const auto cursor = os::Window::decodeCursorImage(look_b_png_bintex, 8, 8);
            return cursor ? &(*cursor) : nullptr;
        }

        const os::Window::CursorImage* zoomInCursorImage()
        {
            static const auto cursor = os::Window::decodeCursorImage(zoom_in_png_bintex, 8, 8);
            return cursor ? &(*cursor) : nullptr;
        }

        const os::Window::CursorImage* zoomOutCursorImage()
        {
            static const auto cursor = os::Window::decodeCursorImage(zoom_out_png_bintex, 8, 8);
            return cursor ? &(*cursor) : nullptr;
        }

        const os::Window::CursorImage* fpsCursorImage(const os::Window::CursorType cursorType)
        {
            switch (cursorType)
            {
                case os::Window::CursorType::eArrow:
                    return arrowCursorImage();
                case os::Window::CursorType::eOrbit:
                    return orbitCursorImage();
                case os::Window::CursorType::eGrab:
                    return grabCursorImage();
                case os::Window::CursorType::eLook:
                    return lookCursorImage();
                case os::Window::CursorType::eZoomIn:
                    return zoomInCursorImage();
                case os::Window::CursorType::eZoomOut:
                    return zoomOutCursorImage();
                default:
                    return nullptr;
            }
        }

        void finalizeCamera(RenderCamera& cam)
        {
            cam.viewProjection        = cam.projection * cam.view;
            cam.inverseView           = glm::inverse(cam.view);
            cam.inverseProjection     = glm::inverse(cam.projection);
            cam.inverseViewProjection = glm::inverse(cam.viewProjection);

            auto planes = math::extractFrustumPlanes(cam.viewProjection);

            for (int i = 0; i < 6; ++i)
                cam.frustumPlanes[i] = glm::vec4(planes[i].normal, planes[i].d);
        }

        [[nodiscard]] bool xrPoseUsable(const IRenderBackendService::XREyeView& eyeView)
        {
            return eyeView.positionValid && eyeView.orientationValid;
        }

        [[nodiscard]] RenderCamera makeXREyeCamera(const RenderCamera&                     base,
                                                   const IRenderBackendService::XREyeView& eyeView,
                                                   const glm::mat4&                        originTransform)
        {
            RenderCamera    eyeCam        = base;
            const glm::mat4 eyeWorld      = originTransform * eyeView.pose;
            eyeCam.view                   = glm::inverse(eyeWorld);
            eyeCam.projection             = eyeView.projection;
            eyeCam.target                 = eyeView.target;
            eyeCam.viewIndex              = eyeView.eyeIndex;
            eyeCam.viewCount              = 2u;
            eyeCam.xrViewEnabled          = true;
            eyeCam.isXRView               = true;
            eyeCam.isXRPrimaryView        = eyeView.eyeIndex == 0u;
            eyeCam.xrHeadPosition         = glm::vec3(originTransform * glm::vec4(eyeView.headPosition, 1.0f));
            eyeCam.xrEyePosition          = glm::vec3(eyeWorld[3]);
            eyeCam.xrFov                  = eyeView.fov;
            eyeCam.xrIpd                  = eyeView.ipd;
            eyeCam.xrPredictedDisplayTime = eyeView.predictedDisplayTime;
            eyeCam.xrPositionValid        = eyeView.positionValid;
            eyeCam.xrOrientationValid     = eyeView.orientationValid;
            eyeCam.xrPositionTracked      = eyeView.positionTracked;
            eyeCam.xrOrientationTracked   = eyeView.orientationTracked;
            finalizeCamera(eyeCam);
            return eyeCam;
        }

        glm::vec3 makeForward(float yawDegrees, float pitchDegrees)
        {
            const float yawRad   = glm::radians(yawDegrees);
            const float pitchRad = glm::radians(pitchDegrees);

            const glm::vec3 forward {
                std::cos(yawRad) * std::cos(pitchRad),
                std::sin(pitchRad),
                std::sin(yawRad) * std::cos(pitchRad),
            };

            return glm::normalize(forward);
        }

        glm::mat4 makeTransformMatrix(const TransformComponent& transform)
        {
            return glm::translate(glm::mat4 {1.0f}, transform.position) * glm::mat4_cast(transform.rotation) *
                   glm::scale(glm::mat4 {1.0f}, transform.scale);
        }

        glm::mat4 makeWorldTransformMatrix(const entt::registry& reg, const entt::entity entity)
        {
            const auto* transform = reg.try_get<TransformComponent>(entity);
            if (!transform)
                return glm::mat4 {1.0f};

            const auto  local     = makeTransformMatrix(*transform);
            const auto* hierarchy = reg.try_get<HierarchyComponent>(entity);
            if (!hierarchy || hierarchy->parent == entt::null || !reg.valid(hierarchy->parent))
                return local;

            return makeWorldTransformMatrix(reg, hierarchy->parent) * local;
        }

        glm::mat4 makeProjectionMatrix(const CameraComponent& camera, const float aspect)
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
    } // namespace

    bool CameraSystem::onInit()
    {
        VULTRA_CORE_INFO("[CameraSystem] Initializing...");

        VULTRA_CORE_TRACE("[CameraSystem] Providing ICameraService");
        ctx().services.provide<ICameraService>(this);

        VULTRA_CORE_INFO("[CameraSystem] Initialized!");
        return true;
    }

    void CameraSystem::onShutdown()
    {
        VULTRA_CORE_INFO("[CameraSystem] Shutting down");

        resetFPSCursorOverride();
        if (m_MouseCaptureApplied)
        {
            auto& window = ctx().services.require<IWindowService>().window();
            window.setMouseRelativeMode(false).setCursorVisibility(true).setCursor(os::Window::CursorType::eArrow);
            m_MouseCaptureApplied = false;
        }

        m_Cooked.clear();
        m_Manual.clear();
        m_FPSController.reset();
    }

    void CameraSystem::onUpdate(fsec dt) { applyFPSCamera(dt); }

    void CameraSystem::onPreRender()
    {
        // Camera cooking is deferred to cameras() so XR data can be consumed after beginFrame.
    }

    std::span<const RenderCamera> CameraSystem::cameras()
    {
        m_Cooked.clear();

        auto*      backendService = ctx().services.tryGet<IRenderBackendService>();
        const auto xrEyeViews     = (backendService && backendService->isXREnabled()) ?
                                        backendService->xrEyeViews() :
                                        std::span<const IRenderBackendService::XREyeView> {};

        const std::size_t viewMultiplier = xrEyeViews.empty() ? 1u : xrEyeViews.size();
        std::size_t       ecsCameraCount = 0;
        if (auto* worldService = ctx().services.tryGet<IWorldService>())
            ecsCameraCount =
                worldService->world().registry().view<IDComponent, TransformComponent, CameraComponent>().size_hint();
        m_Cooked.reserve((ecsCameraCount + m_Manual.size()) * viewMultiplier);

        const bool cookWorldXR   = m_WorldXRCamerasEnabled && !xrEyeViews.empty();
        const bool cookWorldMono = m_WorldCamerasEnabled;
        if (cookWorldMono || cookWorldXR)
        {
            if (auto* worldService = ctx().services.tryGet<IWorldService>())
            {
                auto&       world  = worldService->world();
                auto&       reg    = world.registry();
                const auto  extent = backendService ? backendService->backbuffer().getExtent() : rhi::Extent2D {1u, 1u};
                const float aspect =
                    static_cast<float>(std::max(extent.width, 1u)) / static_cast<float>(std::max(extent.height, 1u));

                auto view                   = reg.view<IDComponent, TransformComponent, CameraComponent>();
                bool hasActivePrimaryCamera = false;
                for (auto e : view)
                {
                    const auto& camera = view.get<CameraComponent>(e);
                    if (!camera.primary)
                        continue;
                    if (auto* status = reg.try_get<EntityStatusComponent>(e); status && !status->active)
                        continue;

                    hasActivePrimaryCamera = true;
                    break;
                }

                for (auto e : view)
                {
                    const auto& id     = view.get<IDComponent>(e);
                    const auto& camera = view.get<CameraComponent>(e);
                    if (auto* status = reg.try_get<EntityStatusComponent>(e); status && !status->active)
                        continue;
                    if (hasActivePrimaryCamera && !camera.primary)
                        continue;

                    RenderCamera cam {};
                    cam.uuid        = id.uuid;
                    cam.name        = reg.all_of<NameComponent>(e) ? reg.get<NameComponent>(e).name : "Camera";
                    cam.priority    = camera.priority;
                    cam.view        = glm::inverse(makeWorldTransformMatrix(reg, e));
                    cam.projection  = makeProjectionMatrix(camera, aspect);
                    cam.zNear       = std::max(camera.zNear, 0.0001f);
                    cam.zFar        = std::max(camera.zFar, cam.zNear + 0.0001f);
                    cam.fovY        = glm::radians(camera.fovYDegrees);
                    cam.clearValue  = camera.clearColor;
                    cam.clearMode   = camera.clearMode;
                    cam.renderImGui = false;
                    cam.rendererKey = camera.rendererKey.empty() ? "universal" : camera.rendererKey;

                    const auto* xrView      = reg.try_get<XRViewComponent>(e);
                    const bool  wantsXR     = xrView && xrView->enabled;
                    const bool  xrPoseReady = wantsXR && !xrEyeViews.empty() &&
                                             std::all_of(xrEyeViews.begin(), xrEyeViews.end(), xrPoseUsable);
                    if (xrPoseReady && cookWorldXR)
                    {
                        const glm::mat4 originTransform = makeWorldTransformMatrix(reg, e);
                        cam.xrViewEnabled               = true;
                        cam.xrFallbackMono              = xrView ? xrView->fallbackMono : true;
                        for (const auto& eyeView : xrEyeViews)
                            m_Cooked.push_back(makeXREyeCamera(cam, eyeView, originTransform));
                        continue;
                    }

                    if (!cookWorldMono)
                        continue;

                    if (wantsXR && xrView && !xrView->fallbackMono)
                        continue;

                    finalizeCamera(cam);
                    m_Cooked.push_back(std::move(cam));
                }
            }
        }

        for (const auto& srcCam : m_Manual)
        {
            if (srcCam.xrViewEnabled && !xrEyeViews.empty())
            {
                const auto      cookedBefore    = m_Cooked.size();
                const glm::mat4 originTransform = glm::inverse(srcCam.view);
                for (const auto& eyeView : xrEyeViews)
                {
                    if (xrPoseUsable(eyeView))
                        m_Cooked.push_back(makeXREyeCamera(srcCam, eyeView, originTransform));
                }

                if (m_Cooked.size() != cookedBefore)
                    continue;
            }

            if (srcCam.xrViewEnabled && !srcCam.xrFallbackMono)
                continue;

            RenderCamera cam    = srcCam;
            cam.viewIndex       = 0;
            cam.viewCount       = 1;
            cam.isXRView        = false;
            cam.isXRPrimaryView = true;
            finalizeCamera(cam);
            m_Cooked.push_back(std::move(cam));
        }

        return m_Cooked;
    }

    void CameraSystem::clearManualCameras() { m_Manual.clear(); }

    void CameraSystem::setWorldCamerasEnabled(const bool enabled) { m_WorldCamerasEnabled = enabled; }

    void CameraSystem::setWorldXRCamerasEnabled(const bool enabled) { m_WorldXRCamerasEnabled = enabled; }

    RenderCamera& CameraSystem::addManualCamera(const RenderCamera& cam)
    {
        m_Manual.push_back(cam);
        return m_Manual.back();
    }

    void CameraSystem::setFPSCameraController(const FPSCameraController& controller, std::size_t manualCameraIndex)
    {
        m_FPSController        = controller;
        m_FPSManualCameraIndex = manualCameraIndex;
        if (m_FPSController)
        {
            const auto forward             = makeForward(m_FPSController->yawDegrees, m_FPSController->pitchDegrees);
            m_FPSController->orbitDistance = std::max(m_FPSController->orbitDistance, 0.1f);
            // Keep initial pose stable: derive orbit pivot from current pose instead of forcing a fixed pivot.
            m_FPSController->orbitPivot = m_FPSController->position + forward * m_FPSController->orbitDistance;
        }
    }

    void CameraSystem::disableFPSCameraController()
    {
        resetFPSCursorOverride();
        m_FPSController.reset();
        if (m_MouseCaptureApplied)
        {
            auto& window = ctx().services.require<IWindowService>().window();
            window.setMouseRelativeMode(false).setCursorVisibility(true).setCursor(os::Window::CursorType::eArrow);
            m_MouseCaptureApplied = false;
        }
    }

    FPSCameraController* CameraSystem::fpsCameraController() { return m_FPSController ? &(*m_FPSController) : nullptr; }

    const FPSCameraController* CameraSystem::fpsCameraController() const
    {
        return m_FPSController ? &(*m_FPSController) : nullptr;
    }

    std::optional<CameraControlOverlayInfo> CameraSystem::cameraControlOverlayInfo() const
    {
        if (!m_FPSController)
            return std::nullopt;
        return CameraControlOverlayInfo {
            .enabled = m_FPSController->enabled,
            .mode    = m_ActiveControlMode,
        };
    }

    void CameraSystem::setCameraControlInputSuppressed(const bool suppressed) { m_InputSuppressed = suppressed; }

    void CameraSystem::resetFPSCursorOverride()
    {
        auto* windowService = ctx().services.tryGet<IWindowService>();
        if (windowService != nullptr)
        {
            windowService->window().clearCustomCursor();
        }
        m_AppliedFPSCursor          = os::Window::CursorType::eArrow;
        m_TransientFPSCursor        = os::Window::CursorType::eArrow;
        m_HasAppliedFPSCursor       = false;
        m_TransientFPSCursorSeconds = 0.0f;
    }

    void CameraSystem::applyFPSCursor(os::Window& window, const os::Window::CursorType cursorType)
    {
        const auto* cursorImage       = fpsCursorImage(cursorType);
        const bool  wantsCustomCursor = cursorImage != nullptr;
        const bool  hasExpectedCursorState =
            (!wantsCustomCursor && !window.hasCustomCursor()) || (wantsCustomCursor && window.hasCustomCursor());

        if (m_HasAppliedFPSCursor && m_AppliedFPSCursor == cursorType && hasExpectedCursorState)
        {
            return;
        }

        window.setCursor(cursorType);

        if (cursorImage != nullptr)
        {
            window.setCustomCursor(*cursorImage);
        }
        else
        {
            window.clearCustomCursor();
        }

        m_AppliedFPSCursor    = cursorType;
        m_HasAppliedFPSCursor = true;
    }

    void CameraSystem::applyFPSCamera(fsec dt)
    {
        if (!m_FPSController)
            return;

        auto& input                 = ctx().services.require<IInputService>();
        auto& controller            = *m_FPSController;
        m_TransientFPSCursorSeconds = std::max(0.0f, m_TransientFPSCursorSeconds - static_cast<float>(dt.count()));

        if (m_InputSuppressed)
        {
            m_ActiveControlMode = CameraControlMode::eOrbit;
            resetFPSCursorOverride();
            if (m_MouseCaptureApplied)
            {
                auto& window = ctx().services.require<IWindowService>().window();
                window.setMouseRelativeMode(false).setCursorVisibility(true).setCursor(os::Window::CursorType::eArrow);
                m_MouseCaptureApplied = false;
            }
            else
            {
                auto& window = ctx().services.require<IWindowService>().window();
                window.setCursor(os::Window::CursorType::eArrow);
            }
            return;
        }

        if (!controller.enabled)
        {
            m_ActiveControlMode = CameraControlMode::eDisabled;
            resetFPSCursorOverride();
            if (m_MouseCaptureApplied)
            {
                auto& window = ctx().services.require<IWindowService>().window();
                window.setMouseRelativeMode(false).setCursorVisibility(true).setCursor(os::Window::CursorType::eArrow);
                m_MouseCaptureApplied = false;
            }
            else
            {
                auto& window = ctx().services.require<IWindowService>().window();
                window.setCursor(os::Window::CursorType::eArrow);
            }
            return;
        }

        if (m_Manual.empty() || m_FPSManualCameraIndex >= m_Manual.size())
            return;

        auto& camera = m_Manual[m_FPSManualCameraIndex];

        auto&      window      = ctx().services.require<IWindowService>().window();
        const bool flyActive   = input.getMouseButton(MouseCode::eRight);
        const bool shiftHeld   = input.getKey(KeyCode::eLShift) || input.getKey(KeyCode::eRShift);
        const bool orbitActive = input.getMouseButton(MouseCode::eLeft) && !shiftHeld;
        const bool panActive =
            input.getMouseButton(MouseCode::eMiddle) || (input.getMouseButton(MouseCode::eLeft) && shiftHeld);
        const float scrollY = input.getMouseScrollDelta().y;
        m_ActiveControlMode = flyActive ? CameraControlMode::eFly : CameraControlMode::eOrbit;

        if (std::abs(scrollY) > 0.0f)
        {
            m_TransientFPSCursor = scrollY > 0.0f ? os::Window::CursorType::eZoomIn : os::Window::CursorType::eZoomOut;
            m_TransientFPSCursorSeconds = 0.18f;
        }

        if (flyActive)
            applyFPSCursor(window, os::Window::CursorType::eLook);
        else if (panActive)
            applyFPSCursor(window, os::Window::CursorType::eGrab);
        else if (orbitActive)
            applyFPSCursor(window, os::Window::CursorType::eOrbit);
        else if (m_TransientFPSCursorSeconds > 0.0f)
            applyFPSCursor(window, m_TransientFPSCursor);
        else
            applyFPSCursor(window, os::Window::CursorType::eArrow);

        if (controller.captureMouse && flyActive)
        {
            if (!m_MouseCaptureApplied)
            {
                window.setMouseRelativeMode(true).setCursorVisibility(false);
                m_MouseCaptureApplied = true;
            }
        }
        else if (m_MouseCaptureApplied)
        {
            window.setMouseRelativeMode(false).setCursorVisibility(true);
            m_MouseCaptureApplied = false;
        }

        const glm::vec2 mouseDelta = input.getMousePositionDelta();
        if (flyActive)
        {
            controller.yawDegrees += mouseDelta.x * controller.mouseSensitivity;
            controller.pitchDegrees -= mouseDelta.y * controller.mouseSensitivity;
        }
        else if (input.getMouseButton(MouseCode::eLeft) && !shiftHeld)
        {
            controller.yawDegrees += mouseDelta.x * controller.orbitRotateSensitivity;
            controller.pitchDegrees -= mouseDelta.y * controller.orbitRotateSensitivity;
        }
        controller.pitchDegrees = std::clamp(controller.pitchDegrees, -89.0f, 89.0f);

        glm::vec3 forward = makeForward(controller.yawDegrees, controller.pitchDegrees);
        glm::vec3 right   = glm::normalize(glm::cross(forward, kWorldUp));
        glm::vec3 up      = glm::normalize(glm::cross(right, forward));

        if (flyActive)
        {
            glm::vec3 moveDir {0.0f};
            if (input.getKey(KeyCode::eW))
                moveDir += forward;
            if (input.getKey(KeyCode::eS))
                moveDir -= forward;
            if (input.getKey(KeyCode::eD))
                moveDir += right;
            if (input.getKey(KeyCode::eA))
                moveDir -= right;
            if (input.getKey(KeyCode::eE))
                moveDir += kWorldUp;
            if (input.getKey(KeyCode::eQ))
                moveDir -= kWorldUp;

            float speed = controller.moveSpeed;
            if (input.getKey(KeyCode::eLShift) || input.getKey(KeyCode::eRShift))
                speed *= controller.sprintMultiplier;
            if (input.getKey(KeyCode::eLCtrl) || input.getKey(KeyCode::eRCtrl))
                speed *= 0.35f;

            if (glm::dot(moveDir, moveDir) > 0.0f)
                controller.position += glm::normalize(moveDir) * speed * dt.count();
            controller.orbitPivot = controller.position + forward * std::max(controller.orbitDistance, 0.1f);
        }
        else
        {
            const bool panActive =
                input.getMouseButton(MouseCode::eMiddle) || (input.getMouseButton(MouseCode::eLeft) && shiftHeld);
            if (panActive)
            {
                const float panScale = controller.orbitPanSensitivity * std::max(controller.orbitDistance, 0.1f);
                controller.orbitPivot -= right * (mouseDelta.x * panScale);
                controller.orbitPivot += up * (mouseDelta.y * panScale);
            }

            if (std::abs(scrollY) > 0.0f)
            {
                const float zoomFactor = std::exp(-scrollY * controller.orbitZoomSpeed);
                controller.orbitDistance =
                    std::clamp(controller.orbitDistance * zoomFactor, 0.1f, std::max(controller.zFar * 0.9f, 10.0f));
            }

            controller.position = controller.orbitPivot - forward * std::max(controller.orbitDistance, 0.1f);
        }

        const auto  extent = window.platformType() == os::Window::PlatformType::eAndroidNativeWindow ?
                                 os::Window::Extent {static_cast<int>(window.getContentArea().extent.width),
                                                    static_cast<int>(window.getContentArea().extent.height)} :
                                 window.getExtent();
        const float width  = static_cast<float>(std::max(extent.x, 1));
        const float height = static_cast<float>(std::max(extent.y, 1));
        const float aspect = width / height;

        forward     = makeForward(controller.yawDegrees, controller.pitchDegrees);
        camera.view = glm::lookAt(controller.position, controller.position + forward, kWorldUp);
        camera.projection =
            glm::perspectiveRH_ZO(glm::radians(controller.fovYDegrees), aspect, controller.zNear, controller.zFar);

        camera.fovY  = glm::radians(controller.fovYDegrees);
        camera.zNear = controller.zNear;
        camera.zFar  = controller.zFar;
    }
} // namespace vultra
