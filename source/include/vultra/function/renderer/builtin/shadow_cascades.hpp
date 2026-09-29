#pragma once

#include <vultra/function/camera/camera.hpp>

#include <array>
#include <cstdint>

namespace vultra
{
    struct ShadowCascades
    {
        std::array<glm::mat4, 4> viewProjection;
        glm::vec4                splits;
        glm::vec4                worldWidths;
        glm::vec4                depthRanges;
    };

    ShadowCascades calculateCascades(const RenderCamera& camera,
                                     glm::vec3           directionToLight,
                                     glm::vec3           casterCenter,
                                     float               casterRadius,
                                     uint32_t            resolution,
                                     float               splitLambda);
} // namespace vultra
