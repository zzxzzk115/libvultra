#pragma once

#include <glm/glm.hpp>

namespace vultra
{
    struct RenderCamera
    {
        glm::mat4 view {1};
        glm::mat4 projection {1}; // Right-handed, Y up, depth [0,1].
        float     nearPlane = 0.01f;
        float     farPlane  = 100;
    };
} // namespace vultra
