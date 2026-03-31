#include "vultra/function/camera/camera_system.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/engine/engine_context.hpp"
#include "vultra/core/math/math.hpp"
#include "vultra/core/services/input_service.hpp"
#include "vultra/core/services/window_service.hpp"
#include "vultra/function/services/render_backend_service.hpp"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>

namespace vultra
{
    namespace
    {
        constexpr glm::vec3 kWorldUp {0.0f, 1.0f, 0.0f};

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

        if (m_MouseCaptureApplied)
        {
            auto& window = ctx().services.require<IWindowService>().window();
            window.setMouseRelativeMode(false).setCursorVisibility(true);
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
        m_Cooked.reserve(m_Manual.size() * viewMultiplier);

        for (const auto& srcCam : m_Manual)
        {
            if (!xrEyeViews.empty())
            {
                for (const auto& eyeView : xrEyeViews)
                {
                    RenderCamera cam    = srcCam;
                    cam.view            = eyeView.view;
                    cam.projection      = eyeView.projection;
                    cam.target          = eyeView.target;
                    cam.viewIndex       = eyeView.eyeIndex;
                    cam.viewCount       = static_cast<uint32_t>(xrEyeViews.size());
                    cam.isXRView        = true;
                    cam.isXRPrimaryView = eyeView.eyeIndex == 0u;
                    finalizeCamera(cam);
                    m_Cooked.push_back(std::move(cam));
                }

                continue;
            }

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

    RenderCamera& CameraSystem::addManualCamera(const RenderCamera& cam)
    {
        m_Manual.push_back(cam);
        return m_Manual.back();
    }

    void CameraSystem::setFPSCameraController(const FPSCameraController& controller, std::size_t manualCameraIndex)
    {
        m_FPSController        = controller;
        m_FPSManualCameraIndex = manualCameraIndex;
    }

    void CameraSystem::disableFPSCameraController()
    {
        m_FPSController.reset();
        if (m_MouseCaptureApplied)
        {
            auto& window = ctx().services.require<IWindowService>().window();
            window.setMouseRelativeMode(false).setCursorVisibility(true);
            m_MouseCaptureApplied = false;
        }
    }

    FPSCameraController* CameraSystem::fpsCameraController() { return m_FPSController ? &(*m_FPSController) : nullptr; }

    const FPSCameraController* CameraSystem::fpsCameraController() const
    {
        return m_FPSController ? &(*m_FPSController) : nullptr;
    }

    void CameraSystem::applyFPSCamera(fsec dt)
    {
        if (!m_FPSController)
            return;

        auto& input = ctx().services.require<IInputService>();
        if (input.getMouseButtonDown(MouseCode::eRight))
            m_FPSController->enabled = !m_FPSController->enabled;

        if (!m_FPSController->enabled)
        {
            if (m_MouseCaptureApplied)
            {
                auto& window = ctx().services.require<IWindowService>().window();
                window.setMouseRelativeMode(false).setCursorVisibility(true);
                m_MouseCaptureApplied = false;
            }
            return;
        }

        if (m_Manual.empty() || m_FPSManualCameraIndex >= m_Manual.size())
            return;

        auto& controller = *m_FPSController;
        auto& camera     = m_Manual[m_FPSManualCameraIndex];

        auto& window = ctx().services.require<IWindowService>().window();

        if (controller.captureMouse)
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
        controller.yawDegrees += mouseDelta.x * controller.mouseSensitivity;
        controller.pitchDegrees -= mouseDelta.y * controller.mouseSensitivity;
        controller.pitchDegrees = std::clamp(controller.pitchDegrees, -89.0f, 89.0f);

        const glm::vec3 forward = makeForward(controller.yawDegrees, controller.pitchDegrees);
        const glm::vec3 right   = glm::normalize(glm::cross(forward, kWorldUp));

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

        if (glm::dot(moveDir, moveDir) > 0.0f)
            controller.position += glm::normalize(moveDir) * speed * dt.count();

        const auto  extent = window.platformType() == os::Window::PlatformType::eAndroidNativeWindow ?
                                 os::Window::Extent {static_cast<int>(window.getContentArea().extent.width),
                                                    static_cast<int>(window.getContentArea().extent.height)} :
                                 window.getExtent();
        const float width  = static_cast<float>(std::max(extent.x, 1));
        const float height = static_cast<float>(std::max(extent.y, 1));
        const float aspect = width / height;

        camera.view = glm::lookAt(controller.position, controller.position + forward, kWorldUp);
        camera.projection =
            glm::perspective(glm::radians(controller.fovYDegrees), aspect, controller.zNear, controller.zFar);
        camera.projection[1][1] *= -1.0f; // Vulkan clip space adjustment

        camera.fovY  = glm::radians(controller.fovYDegrees);
        camera.zNear = controller.zNear;
        camera.zFar  = controller.zFar;
    }
} // namespace vultra
