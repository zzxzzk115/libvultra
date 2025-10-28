#pragma once

#include "vultra/function/scenegraph/components.hpp"

namespace vultra
{
    glm::mat4 getCameraViewMatrix(const TransformComponent& camTransform);
    glm::mat4 getCameraProjectionMatrix(CameraComponent& camComponent, bool invertY = true);
} // namespace vultra