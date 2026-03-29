#pragma once

#include "vultra/core/engine/engine_subsystem.hpp"
#include "vultra/function/rendering/render_structs.hpp"
#include "vultra/function/services/camera_service.hpp"

#include <glm/vec3.hpp>

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace vultra
{
    struct FPSCameraController
    {
        bool enabled {true};
        bool captureMouse {true};

        float moveSpeed {4.0f};
        float sprintMultiplier {2.5f};
        float mouseSensitivity {0.08f}; // degrees per pixel

        glm::vec3 position {0.0f, 1.0f, 1.5f};
        float     yawDegrees {-90.0f};
        float     pitchDegrees {-20.0f};

        float fovYDegrees {60.0f};
        float zNear {0.1f};
        float zFar {1000.0f};
    };

    // - Iterate ECS CameraComponent / XrCameraComponent
    // - Cook into RenderCamera list each frame
    class CameraSystem final : public EngineSubsystem, public ICameraService
    {
    public:
        const char* name() const override { return "CameraSystem"; }

        bool onInit() override;
        void onShutdown() override;
        void onUpdate(fsec dt) override;

        // Tick: rebuild cooked camera list
        void onPreRender() override;

        // ICameraService
        std::span<const RenderCamera> cameras() override;

        // Incremental helper (until ECS cooking is wired):
        // App can push cameras manually.
        void          clearManualCameras();
        RenderCamera& addManualCamera(const RenderCamera& cam);

        void setFPSCameraController(const FPSCameraController& controller, std::size_t manualCameraIndex = 0);
        void disableFPSCameraController();

        bool                       hasFPSCameraController() const { return m_FPSController.has_value(); }
        FPSCameraController*       fpsCameraController();
        const FPSCameraController* fpsCameraController() const;

    private:
        void applyFPSCamera(fsec dt);

        // Cooked list for current frame
        std::vector<RenderCamera> m_Cooked;

        // Temporary manual input list
        std::vector<RenderCamera> m_Manual;

        std::optional<FPSCameraController> m_FPSController;
        std::size_t                        m_FPSManualCameraIndex {0};
        bool                               m_MouseCaptureApplied {false};
    };
} // namespace vultra
