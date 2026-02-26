#include "vultra/function/camera/camera_system.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/engine/engine_context.hpp"

namespace vultra
{
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
        // Incremental: just forward manual list -> cooked list.
        // Replace with ECS cooking later.
        m_Cooked = m_Manual;
    }

    void CameraSystem::clearManualCameras() { m_Manual.clear(); }

    void CameraSystem::addManualCamera(const RenderCamera& cam) { m_Manual.push_back(cam); }
} // namespace vultra
