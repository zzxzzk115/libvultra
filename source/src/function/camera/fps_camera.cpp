#include "vultra/function/camera/fps_camera.hpp"
#include "vultra/core/input/input.hpp"
#include "vultra/function/scenegraph/components.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <imgui.h>

namespace vultra
{
    FirstPersonShooterCamera::FirstPersonShooterCamera(TransformComponent* transformComponent) :
        m_TransformComponent(transformComponent)
    {
        assert(m_TransformComponent);
        m_BackupPosition = m_Position = m_TransformComponent->position;
        m_BackupRotationEuler = m_RotationEuler = m_TransformComponent->getRotationEuler();
    }

    FirstPersonShooterCamera::FirstPersonShooterCamera(const glm::vec3& position, const glm::vec3& rotationEuler) :
        m_TransformComponent(nullptr), m_Position(position), m_RotationEuler(rotationEuler)
    {
        m_BackupPosition      = position;
        m_BackupRotationEuler = rotationEuler;
    }

    void FirstPersonShooterCamera::onUpdate(const fsec dt)
    {
        if (!m_EnableCameraControl)
            return;

        float cameraMovement = m_MovementSpeed * dt.count();

        if (Input::getKey(KeyCode::eLShift))
            cameraMovement *= 2.0f;

        if (Input::getKey(KeyCode::eW))
            m_Position += cameraMovement * forward();

        if (Input::getKey(KeyCode::eS))
            m_Position -= cameraMovement * forward();

        if (Input::getKey(KeyCode::eA))
            m_Position -= right() * cameraMovement;

        if (Input::getKey(KeyCode::eD))
            m_Position += right() * cameraMovement;

        if (Input::getKey(KeyCode::eQ))
            m_Position -= up() * cameraMovement;

        if (Input::getKey(KeyCode::eE))
            m_Position += up() * cameraMovement;

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

            auto& euler = m_RotationEuler;
            euler.y += xOffset;
            euler.x += yOffset;

            // Constrain pitch
            if (euler.x > 89.0f)
                euler.x = 89.0f;
            if (euler.x < -89.0f)
                euler.x = -89.0f;
        }
        else
        {
            m_FirstMouse = true;
        }

        if (m_TransformComponent)
        {
            m_TransformComponent->position = m_Position;
            m_TransformComponent->setRotationEuler(m_RotationEuler);
        }
    }

    void FirstPersonShooterCamera::onImGui()
    {
        if (ImGui::CollapsingHeader("First Person Shooter Camera"))
        {
            ImGui::Text("press WASD to move, QE to down/up; drag right mouse button to look around");
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
        m_Position            = m_BackupPosition;
        m_RotationEuler       = m_BackupRotationEuler;
        m_EnableCameraControl = true;
        m_FirstMouse          = true;
        m_LastX               = 0.0f;
        m_LastY               = 0.0f;
    }

    glm::vec3 FirstPersonShooterCamera::getPosition() const
    {
        return m_TransformComponent ? m_TransformComponent->position : m_Position;
    }

    glm::vec3 FirstPersonShooterCamera::getRotationEuler() const
    {
        return m_TransformComponent ? m_TransformComponent->getRotationEuler() : m_RotationEuler;
    }

    glm::vec3 FirstPersonShooterCamera::forward() const
    {
        return m_TransformComponent ? m_TransformComponent->forward() :
                                      glm::quat(glm::radians(m_RotationEuler)) * glm::vec3(0, 0, -1);
    }

    glm::vec3 FirstPersonShooterCamera::right() const
    {
        return m_TransformComponent ? m_TransformComponent->right() :
                                      glm::quat(glm::radians(m_RotationEuler)) * glm::vec3(1, 0, 0);
    }

    glm::vec3 FirstPersonShooterCamera::up() const
    {
        return m_TransformComponent ? m_TransformComponent->up() :
                                      glm::quat(glm::radians(m_RotationEuler)) * glm::vec3(0, 1, 0);
    }
} // namespace vultra