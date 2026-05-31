#pragma once

#include <glm/vec3.hpp>

namespace vultra
{
    struct BoxShapeComponent
    {
        glm::vec3 halfExtents {0.5f};
    };
} // namespace vultra
