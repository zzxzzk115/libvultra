#include "vultra/function/camera/fps_camera.hpp"
#include "vultra/core/input/input.hpp"
#include "vultra/function/scenegraph/components.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <imgui.h>

namespace vultra
{
    FirstPersonShooterCamera::FirstPersonShooterCamera(CameraComponent&    cameraComponent,
                                                       TransformComponent& transformComponent) :
        m_CameraComponent(cameraComponent), m_TransformComponent(transformComponent)
    {
        m_BackupPosition      = m_TransformComponent.position;
        m_BackupRotationEuler = m_TransformComponent.getRotationEuler();
    }

    void FirstPersonShooterCamera::onUpdate(const fsec dt)
    {
        if (!m_EnableCameraControl)
            return;

        float cameraMovement = m_MovementSpeed * dt.count();

        if (Input::getKey(KeyCode::eLShift))
            cameraMovement *= 2.0f;

        if (Input::getKey(KeyCode::eW))
            m_TransformComponent.position += cameraMovement * m_TransformComponent.forward();

        if (Input::getKey(KeyCode::eS))
            m_TransformComponent.position -= cameraMovement * m_TransformComponent.forward();

        if (Input::getKey(KeyCode::eA))
            m_TransformComponent.position -= m_TransformComponent.right() * cameraMovement;

        if (Input::getKey(KeyCode::eD))
            m_TransformComponent.position += m_TransformComponent.right() * cameraMovement;

        if (Input::getKey(KeyCode::eQ))
            m_TransformComponent.position -= m_TransformComponent.up() * cameraMovement;

        if (Input::getKey(KeyCode::eE))
            m_TransformComponent.position += m_TransformComponent.up() * cameraMovement;

        if (Input::getMouseButton(MouseCode::eRight))
        {
            static auto mousePos        = Input::getMousePosition();
            auto        currentMousePos = Input::getMousePosition();

            if (m_FirstMouse)
            {
                m_LastX      = currentMousePos.x;
                m_LastY      = currentMousePos.y;
                m_FirstMouse = false;
            }

            float xOffset = m_LastX - currentMousePos.x;
            float yOffset = m_LastY - currentMousePos.y;

            m_LastX = currentMousePos.x;
            m_LastY = currentMousePos.y;

            xOffset *= m_MouseSensitivity;
            yOffset *= m_MouseSensitivity;

            glm::vec3 euler = m_TransformComponent.getRotationEuler();
            euler.y += xOffset;
            euler.x += yOffset;

            // Constrain pitch
            if (euler.x > 89.0f)
                euler.x = 89.0f;
            if (euler.x < -89.0f)
                euler.x = -89.0f;

            m_TransformComponent.setRotationEuler(euler);
        }
        else
        {
            m_FirstMouse = true;
        }
    }

    void FirstPersonShooterCamera::onImGui()
    {
        if (ImGui::CollapsingHeader("First Person Shooter Camera"))
        {
            ImGui::Text("WASD to move, right mouse button to look around");
            ImGui::DragFloat("Movement Speed", &m_MovementSpeed, 0.1f, 0.1f, 100.0f, "%.1f");
            ImGui::DragFloat("Mouse Sensitivity", &m_MouseSensitivity, 0.01f, 0.01f, 1.0f, "%.2f");
            if (ImGui::Button("Reset"))
            {
                reset();
            }
        }
    }

    void FirstPersonShooterCamera::reset()
    {
        m_TransformComponent.position = m_BackupPosition;
        m_TransformComponent.setRotationEuler(m_BackupRotationEuler);
        m_EnableCameraControl = true;
        m_FirstMouse          = true;
        m_LastX               = 0.0f;
        m_LastY               = 0.0f;
    }
} // namespace vultra