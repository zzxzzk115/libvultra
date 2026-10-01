#pragma once

#include <vultra/core/math/extent.hpp>
#include <vultra/core/math/render_camera.hpp>
#include <vultra/platform/input/input.hpp>

namespace vultra
{
    // Left drag orbits, middle/right drag pans, wheel dollies towards the target.
    struct OrbitCamera
    {
        glm::vec3 center {0};
        float     radius            = 1;
        float     distance          = 5;
        float     yaw               = 0.65f;
        float     pitch             = 0.35f;
        float     verticalFov       = glm::radians(50.0f);
        float     rotateSensitivity = 0.005f;
        float     zoomSensitivity   = 0.12f;

        // viewport uses logical window units, matching Input's mouse delta. Angles are radians.
        void         update(const Input& input, Extent viewport, InputCapture capture = {});
        glm::vec3    position() const;
        glm::mat4    view() const;
        float        nearPlane() const;
        float        farPlane(float minimumFar = 0) const;
        glm::mat4    projection(Extent size, float minimumFar = 0) const;
        RenderCamera camera(Extent size, float minimumFar = 0) const;
    };
} // namespace vultra
