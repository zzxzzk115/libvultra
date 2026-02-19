#include "vultra/function/camera/camera_system.hpp"
#include "vultra/core/engine/engine_context.hpp"

namespace vultra
{
    bool CameraSystem::onInit()
    {
        ctx().services.provide<ICameraService>(this);
        return true;
    }

    void CameraSystem::onShutdown()
    {
        m_Cooked.clear();
        m_Manual.clear();
    }

    void CameraSystem::onPreRender()
    {
        // Incremental: just forward manual list -> cooked list.
        // Replace with ECS cooking later.
        m_Cooked = m_Manual;
    }

    void CameraSystem::clearManualCameras() { m_Manual.clear(); }

    void CameraSystem::addManualCamera(const RenderCamera& cam) { m_Manual.push_back(cam); }
} // namespace vultra
