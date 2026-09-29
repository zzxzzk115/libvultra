#include <vultra/function/camera/orbit_camera.hpp>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace vultra
{
    void OrbitCamera::update(const Input& input, Extent viewport, InputCapture capture)
    {
        if (!input.focused() || capture.mouse || viewport.empty())
        {
            return;
        }
        const auto delta = input.mousePositionDelta();
        if (input.isMouseButtonHeld(MouseCode::eLeft) && !input.isMouseButtonPressed(MouseCode::eLeft))
        {
            yaw -= delta.x * rotateSensitivity;
            pitch = std::clamp(pitch + delta.y * rotateSensitivity, -1.5f, 1.5f);
        }
        const bool pan =
            (input.isMouseButtonHeld(MouseCode::eMiddle) && !input.isMouseButtonPressed(MouseCode::eMiddle)) ||
            (input.isMouseButtonHeld(MouseCode::eRight) && !input.isMouseButtonPressed(MouseCode::eRight));
        if (pan)
        {
            const auto  forward       = glm::normalize(center - position());
            const auto  right         = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));
            const auto  up            = glm::cross(right, forward);
            const float unitsPerPixel = 2 * distance * std::tan(verticalFov * 0.5f) / viewport.height;
            center += (-delta.x * right + delta.y * up) * unitsPerPixel;
        }
        distance = std::clamp(distance * std::exp(-input.mouseScrollDelta().y * zoomSensitivity),
                              radius * 0.15f,
                              radius * 30.0f);
    }

    glm::vec3 OrbitCamera::position() const
    {
        return center +
               distance * glm::vec3(std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch));
    }

    glm::mat4 OrbitCamera::view() const
    {
        return glm::lookAtRH(position(), center, glm::vec3(0, 1, 0));
    }

    float OrbitCamera::nearPlane() const
    {
        return radius * 0.005f;
    }

    float OrbitCamera::farPlane(float minimumFar) const
    {
        return std::max(minimumFar, distance + radius * 4);
    }

    glm::mat4 OrbitCamera::projection(Extent size, float minimumFar) const
    {
        return glm::perspectiveRH_ZO(verticalFov, float(size.width) / size.height, nearPlane(), farPlane(minimumFar));
    }

    RenderCamera OrbitCamera::camera(Extent size, float minimumFar) const
    {
        return {view(), projection(size, minimumFar), nearPlane(), farPlane(minimumFar)};
    }
} // namespace vultra
