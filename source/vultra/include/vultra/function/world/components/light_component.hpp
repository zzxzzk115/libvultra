#pragma once

#include <glm/vec3.hpp>

#include <cstdint>

namespace vultra
{
    // Numeric light kind keeps .vscn v1 serialization simple:
    // 0 = directional, 1 = point, 2 = spot, 3 = rectangle area.
    struct LightComponent
    {
        uint32_t kind {0};

        glm::vec3 color {1.0f};
        float     intensity {8.0f};

        // Directional lights use this direction directly. Point/area lights use TransformComponent position.
        glm::vec3 direction {-0.35f, -0.8f, -0.25f};
        float     range {10.0f};

        float radius {0.05f};
        float width {1.0f};
        float height {1.0f};
        float innerConeDegrees {20.0f};
        float outerConeDegrees {30.0f};

        bool castsShadow {false};
        bool twoSided {false};
    };
} // namespace vultra
