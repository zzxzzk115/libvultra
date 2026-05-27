#include "vultra/function/openxr/xr_runtime_system.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/engine/engine_context.hpp"
#include "vultra/function/services/render_backend_service.hpp"
#include "vultra/function/services/world_service.hpp"
#include "vultra/function/world/components/camera_component.hpp"
#include "vultra/function/world/components/entity_status_component.hpp"
#include "vultra/function/world/components/xr_view_component.hpp"
#include "vultra/function/world/world.hpp"

#include <limits>

namespace vultra
{
    bool XRRuntimeSystem::onInit()
    {
        VULTRA_CORE_INFO("[XRRuntimeSystem] Initialized");
        return true;
    }

    void XRRuntimeSystem::onShutdown()
    {
        if (auto* backendService = ctx().services.tryGet<IRenderBackendService>())
            backendService->requestXRSession(false);
        m_LastRequested = false;
    }

    void XRRuntimeSystem::onUpdate(fsec /*dt*/)
    {
        if (!ctx().config.render.xr.autoStartSessionFromScene)
            return;

        auto* backendService = ctx().services.tryGet<IRenderBackendService>();
        if (!backendService)
            return;

        const bool requested = sceneRequestsXR();
        backendService->requestXRSession(requested);
        if (requested != m_LastRequested)
        {
            VULTRA_CORE_INFO("[XRRuntimeSystem] XR session {}", requested ? "requested" : "released");
            m_LastRequested = requested;
        }
    }

    bool XRRuntimeSystem::sceneRequestsXR() const
    {
        auto* worldService = ctx().services.tryGet<IWorldService>();
        if (!worldService)
            return false;

        auto& reg = worldService->world().registry();
        auto  cameras = reg.view<CameraComponent>();

        bool hasPrimaryCamera = false;
        int  bestPriority = std::numeric_limits<int>::min();
        bool bestWantsXR  = false;

        for (auto entity : cameras)
        {
            const auto& camera = cameras.get<CameraComponent>(entity);
            if (auto* status = reg.try_get<EntityStatusComponent>(entity); status && !status->active)
                continue;
            if (!camera.primary)
                continue;

            const auto* xrView = reg.try_get<XRViewComponent>(entity);
            hasPrimaryCamera = true;
            if (camera.priority >= bestPriority)
            {
                bestPriority = camera.priority;
                bestWantsXR  = xrView && xrView->enabled;
            }
        }

        if (hasPrimaryCamera)
            return bestWantsXR;

        auto xrViews = reg.view<CameraComponent, XRViewComponent>();
        for (auto entity : xrViews)
        {
            const auto& xrView = xrViews.get<XRViewComponent>(entity);
            if (auto* status = reg.try_get<EntityStatusComponent>(entity); status && !status->active)
                continue;
            if (xrView.enabled)
                return true;
        }

        return false;
    }
} // namespace vultra
