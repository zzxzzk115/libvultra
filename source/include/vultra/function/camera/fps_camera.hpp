#pragma once

#include "vultra/core/base/base.hpp"

#include <glm/glm.hpp>

namespace vultra
{
    struct CameraComponent;
    struct TransformComponent;

    class FirstPersonShooterCamera
    {
    public:
        FirstPersonShooterCamera(CameraComponent&, TransformComponent&);
        ~FirstPersonShooterCamera() = default;

        void onUpdate(const fsec dt);
        void onImGui();

        void reset();

        void enableCameraControl(bool enable) { m_EnableCameraControl = enable; }
        bool isCameraControlEnabled() const { return m_EnableCameraControl; }

        [[nodiscard]] float getMovementSpeed() const { return m_MovementSpeed; }
        void                setMovementSpeed(float speed) { m_MovementSpeed = speed; }

        [[nodiscard]] float getMouseSensitivity() const { return m_MouseSensitivity; }
        void                setMouseSensitivity(float sensitivity) { m_MouseSensitivity = sensitivity; }

    private:
        CameraComponent&    m_CameraComponent;
        TransformComponent& m_TransformComponent;

        float m_MovementSpeed {5.0f};
        float m_MouseSensitivity {0.1f};

        bool  m_FirstMouse {true};
        float m_LastX {0.0f};
        float m_LastY {0.0f};

        glm::vec3 m_BackupPosition;
        glm::vec3 m_BackupRotationEuler;

        bool m_EnableCameraControl {true};
    };
} // namespace vultra