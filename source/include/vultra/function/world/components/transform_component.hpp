#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace vultra
{
    struct TransformComponent
    {
        glm::vec3 position {0.0f};
        glm::quat rotation {0.0f, 0.0f, 0.0f, 1.0f};
        glm::vec3 scale {1.0f};

        // Optional runtime cache (not serialized in .vscn)
        glm::mat4 worldMatrix {1.0f};
        bool      dirty {true};
    };
} // namespace vultra
