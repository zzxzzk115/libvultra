#include "vultra/function/camera/camera_system.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/engine/engine_context.hpp"

#include <glm/geometric.hpp>

namespace vultra
{
    namespace
    {
        [[nodiscard]] glm::vec4 normalizePlane(const glm::vec4 p)
        {
            const float len = glm::length(glm::vec3(p));
            return len > 0.0f ? p / len : p;
        }

        void finalizeCamera(RenderCamera& cam)
        {
            cam.viewProjection        = cam.projection * cam.view;
            cam.inverseView           = glm::inverse(cam.view);
            cam.inverseProjection     = glm::inverse(cam.projection);
            cam.inverseViewProjection = glm::inverse(cam.viewProjection);

            const glm::mat4& m = cam.viewProjection;

            cam.frustumPlanes[0] = normalizePlane(
                glm::vec4(m[0][3] + m[0][0], m[1][3] + m[1][0], m[2][3] + m[2][0], m[3][3] + m[3][0])); // left
            cam.frustumPlanes[1] = normalizePlane(
                glm::vec4(m[0][3] - m[0][0], m[1][3] - m[1][0], m[2][3] - m[2][0], m[3][3] - m[3][0])); // right
            cam.frustumPlanes[2] = normalizePlane(
                glm::vec4(m[0][3] + m[0][1], m[1][3] + m[1][1], m[2][3] + m[2][1], m[3][3] + m[3][1])); // bottom
            cam.frustumPlanes[3] = normalizePlane(
                glm::vec4(m[0][3] - m[0][1], m[1][3] - m[1][1], m[2][3] - m[2][1], m[3][3] - m[3][1])); // top
            cam.frustumPlanes[4] = normalizePlane(
                glm::vec4(m[0][3] + m[0][2], m[1][3] + m[1][2], m[2][3] + m[2][2], m[3][3] + m[3][2])); // near
            cam.frustumPlanes[5] = normalizePlane(
                glm::vec4(m[0][3] - m[0][2], m[1][3] - m[1][2], m[2][3] - m[2][2], m[3][3] - m[3][2])); // far
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
        m_Cooked.clear();
        m_Manual.clear();
    }

    void CameraSystem::onPreRender()
    {
        m_Cooked = m_Manual;
        for (auto& cam : m_Cooked)
            finalizeCamera(cam);
    }

    void CameraSystem::clearManualCameras() { m_Manual.clear(); }

    RenderCamera& CameraSystem::addManualCamera(const RenderCamera& cam)
    {
        m_Manual.push_back(cam);
        return m_Manual.back();
    }
} // namespace vultra
