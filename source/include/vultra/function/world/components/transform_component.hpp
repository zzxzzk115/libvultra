#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace vultra
{
    struct TransformComponent
    {
        glm::vec3 position {0.0f};
        glm::quat rotation {1.0f, 0.0f, 0.0f, 0.0f}; // w,x,y,z (glm stores as w,x,y,z? actually fields x,y,z,w)
        glm::vec3 scale {1.0f};
    };
} // namespace vultra
