#pragma once

#include <glm/glm.hpp>

#include <cstdint>

namespace vultra
{
    enum class RenderLightKind : uint32_t
    {
        eDirectional,
        ePoint,
        eSpot
    };

    constexpr uint32_t kMaxRenderLights = 64;

    struct RenderLight
    {
        RenderLightKind kind = RenderLightKind::eDirectional;
        glm::vec3       position {0};
        glm::vec3       directionToLight {0, 0, 1};
        glm::vec3       color {1};
        float           intensity = 1;
        float           range     = 10;
        float           innerCone = 0.35f; // Radians, measured from the spotlight axis.
        float           outerCone = 0.6f;
    };
} // namespace vultra
