#include "vultra/function/camera/camera_system.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/engine/engine_context.hpp"
#include "vultra/core/math/math.hpp"

#include <glm/geometric.hpp>

namespace vultra
{
    namespace
    {
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
