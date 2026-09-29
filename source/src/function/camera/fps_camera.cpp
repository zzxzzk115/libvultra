#include <vultra/function/camera/fps_camera.hpp>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace vultra
{
    void FpsCamera::update(const Input& input, float seconds, InputCapture capture)
    {
        if (!input.focused())
        {
            return;
        }
        if (!capture.mouse && input.isMouseButtonHeld(MouseCode::eRight) &&
            !input.isMouseButtonPressed(MouseCode::eRight))
        {
            const auto delta = input.mousePositionDelta();
            yaw += delta.x * lookSensitivity;
            pitch = std::clamp(pitch - delta.y * lookSensitivity, -1.5f, 1.5f);
        }
        if (!capture.keyboard && seconds > 0)
        {
            const auto direction = forward();
            const auto right     = glm::normalize(glm::cross(direction, glm::vec3(0, 1, 0)));
            glm::vec3  movement  = direction * float(input.isKeyHeld(KeyCode::eW) - input.isKeyHeld(KeyCode::eS)) +
                                 right * float(input.isKeyHeld(KeyCode::eD) - input.isKeyHeld(KeyCode::eA));
            movement.y += float(input.isKeyHeld(KeyCode::eE) - input.isKeyHeld(KeyCode::eQ));
            const float length = glm::length(movement);
            if (length > 0)
            {
                const bool fast = input.isKeyHeld(KeyCode::eLShift) || input.isKeyHeld(KeyCode::eRShift);
                position += movement / length * speed * seconds * (fast ? fastMultiplier : 1.0f);
            }
        }
    }

    glm::vec3 FpsCamera::forward() const
    {
        return {std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)};
    }

    glm::mat4 FpsCamera::view() const
    {
        return glm::lookAtRH(position, position + forward(), glm::vec3(0, 1, 0));
    }

    RenderCamera FpsCamera::camera(Extent size, float minimumFar) const
    {
        const float clipFar = std::max(farPlane, minimumFar);
        return {view(),
                glm::perspectiveRH_ZO(verticalFov, float(size.width) / size.height, nearPlane, clipFar),
                nearPlane,
                clipFar};
    }
} // namespace vultra
