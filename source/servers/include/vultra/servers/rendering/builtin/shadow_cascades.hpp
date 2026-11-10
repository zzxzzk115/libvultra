#pragma once

#include <vultra/core/math/render_camera.hpp>

#include <array>
#include <cstdint>
#include <expected>
#include <span>
#include <string>

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

    // All views use the reference camera's depth planes to choose a cascade. Additional
    // views must share its near/far distances; asymmetric and canted projections are supported.
    std::expected<ShadowCascades, std::string>
    calculateCascadesExpected(const RenderCamera&           camera,
                              glm::vec3                     directionToLight,
                              glm::vec3                     casterCenter,
                              float                         casterRadius,
                              uint32_t                      resolution,
                              float                         splitLambda,
                              std::span<const RenderCamera> additionalViews = {});
} // namespace vultra
