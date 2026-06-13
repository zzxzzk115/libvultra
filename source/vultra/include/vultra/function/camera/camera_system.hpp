#pragma once

#include "vultra/core/os/window.hpp"
#include "vultra/core/engine/engine_subsystem.hpp"
#include "vultra/function/rendering/render_structs.hpp"
#include "vultra/function/services/camera_service.hpp"

#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace vultra
{
    struct FPSCameraController
    {
        bool enabled {true};
        bool captureMouse {true};

        float orbitDistance {3.0f};
        float orbitRotateSensitivity {0.08f}; // degrees per pixel
        float orbitPanSensitivity {0.002f};   // world units per pixel (scaled by distance)
        float orbitZoomSpeed {0.03f};         // exponential zoom factor
        glm::vec3 orbitPivot {0.0f, 0.0f, 0.0f};

        float moveSpeed {4.0f};
        float sprintMultiplier {2.5f};
        float mouseSensitivity {0.08f}; // degrees per pixel

        glm::vec3 position {0.0f, 1.0f, 1.5f};
        float     yawDegrees {-90.0f};
        float     pitchDegrees {-20.0f};

        float fovY {60.0f};
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
        std::optional<CameraControlOverlayInfo> cameraControlOverlayInfo() const override;
        void setCameraControlInputSuppressed(bool suppressed) override;

        // Incremental helper (until ECS cooking is wired):
        // App can push cameras manually.
        void          clearManualCameras() override;
        void          removeManualCamerasByName(std::string_view name) override;
        RenderCamera& addManualCamera(const RenderCamera& cam) override;
        void          setWorldCamerasEnabled(bool enabled) override;
        void          setWorldXRCamerasEnabled(bool enabled) override;
        void          setWorldCamerasAllowUpscaler(bool allowed) override { m_WorldCamerasAllowUpscaler = allowed; }

        void setFPSCameraController(const FPSCameraController& controller, std::size_t manualCameraIndex = 0);
        void disableFPSCameraController();

        bool                       hasFPSCameraController() const { return m_FPSController.has_value(); }
        FPSCameraController*       fpsCameraController();
        const FPSCameraController* fpsCameraController() const;

    private:
        void applyFPSCamera(fsec dt);
        void resetFPSCursorOverride();
        void applyFPSCursor(os::Window& window, os::Window::CursorType cursorType);

        // Fills previousView/previousProjection/hasPreviousViewProjection from per-camera
        // history kept across frames. Idempotent within a frame: cameras() is called by
        // several systems per frame and re-cooks the list each time.
        void applyTemporalHistory(RenderCamera& cam);

        // Per-camera temporal history so consumers (motion vectors, upscalers) get real
        // previous-frame matrices. Keyed by camera uuid (or name for nil-uuid manual
        // cameras) combined with viewIndex so XR eyes don't share one entry.
        struct CameraHistoryEntry
        {
            glm::mat4 prevView {1.0f};
            glm::mat4 prevProjection {1.0f};
            glm::mat4 currView {1.0f};
            glm::mat4 currProjection {1.0f};
            uint64_t  stamp {0};
            bool      hasPrev {false};
        };

        // Cooked list for current frame
        std::vector<RenderCamera> m_Cooked;

        std::unordered_map<uint64_t, CameraHistoryEntry> m_History;
        uint64_t                                         m_FrameStamp {0};

        // Temporary manual input list
        std::vector<RenderCamera> m_Manual;
        bool                      m_WorldCamerasEnabled {true};
        bool                      m_WorldXRCamerasEnabled {true};
        bool                      m_WorldCamerasAllowUpscaler {true};

        std::optional<FPSCameraController> m_FPSController;
        std::size_t                        m_FPSManualCameraIndex {0};
        bool                               m_MouseCaptureApplied {false};
        CameraControlMode                  m_ActiveControlMode {CameraControlMode::eDisabled};
        bool                               m_InputSuppressed {false};
        os::Window::CursorType             m_AppliedFPSCursor {os::Window::CursorType::eArrow};
        os::Window::CursorType             m_TransientFPSCursor {os::Window::CursorType::eArrow};
        bool                               m_HasAppliedFPSCursor {false};
        float                              m_TransientFPSCursorSeconds {0.0f};
    };
} // namespace vultra
