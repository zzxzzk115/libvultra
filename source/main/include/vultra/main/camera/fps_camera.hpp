#pragma once

#include <vultra/core/math/extent.hpp>
#include <vultra/core/math/render_camera.hpp>
#include <vultra/platform/input/input.hpp>

namespace vultra
{
    // Right drag looks; WASD moves along the view/right axes, QE along world Y; Shift accelerates.
    struct FpsCamera
    {
        glm::vec3 position {8, 1.5f, -0.5f};
        float     yaw             = glm::radians(-90.0f);
        float     pitch           = 0;
        float     speed           = 3;
        float     lookSensitivity = 0.004f;
        float     fastMultiplier  = 3;
        float     verticalFov     = glm::radians(60.0f);
        float     nearPlane       = 0.05f;
        float     farPlane        = 100;

        void         update(const Input& input, float seconds, InputCapture capture = {});
        glm::vec3    forward() const;
        glm::mat4    view() const;
        RenderCamera camera(Extent size, float minimumFar = 0) const;
    };
} // namespace vultra
