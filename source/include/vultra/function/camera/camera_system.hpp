#pragma once

#include "vultra/core/engine/engine_subsystem.hpp"
#include "vultra/function/rendering/render_camera.hpp"
#include "vultra/function/services/camera_service.hpp"

#include <span>
#include <vector>

namespace vultra
{
    // - Iterate ECS CameraComponent / XrCameraComponent
    // - Cook into RenderCamera list each frame
    class CameraSystem final : public EngineSubsystem, public ICameraService
    {
    public:
        const char* name() const override { return "CameraSystem"; }

        bool onInit() override;
        void onShutdown() override;

        // Tick: rebuild cooked camera list
        void onPreRender() override;

        // ICameraService
        std::span<RenderCamera> cameras() override { return m_Cooked; }

        // Incremental helper (until ECS cooking is wired):
        // App can push cameras manually.
        void clearManualCameras();
        void addManualCamera(const RenderCamera& cam);

    private:
        // Cooked list for current frame
        std::vector<RenderCamera> m_Cooked;

        // Temporary manual input list
        std::vector<RenderCamera> m_Manual;
    };
} // namespace vultra
