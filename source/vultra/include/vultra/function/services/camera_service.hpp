#pragma once

#include <vbase/service/service_registry.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace vultra
{
    struct RenderCamera;

    enum class CameraControlMode : uint8_t
    {
        eDisabled = 0,
        eOrbit    = 1,
        eFly      = 2,
    };

    struct CameraControlOverlayInfo
    {
        bool              enabled {false};
        CameraControlMode mode {CameraControlMode::eDisabled};
    };

    class ICameraService
    {
    public:
        SERVICE_REGISTER(ICameraService);

        // Access cooked cameras for the current frame.
        virtual std::span<const RenderCamera> cameras() = 0;
        virtual void                          clearManualCameras() {}
        virtual void                          removeManualCamerasByName(std::string_view) {}
        virtual RenderCamera&                 addManualCamera(const RenderCamera& camera) = 0;
        virtual void                          setWorldCamerasEnabled(bool) {}
        virtual void                          setWorldXRCamerasEnabled(bool) {}
        virtual std::optional<CameraControlOverlayInfo> cameraControlOverlayInfo() const { return std::nullopt; }
        virtual void setCameraControlInputSuppressed(bool) {}
    };
} // namespace vultra
