#include <vultra/function/renderer/builtin/shadow_cascades.hpp>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace vultra
{
    ShadowCascades calculateCascades(const RenderCamera& camera,
                                     glm::vec3           directionToLight,
                                     glm::vec3           casterCenter,
                                     float               casterRadius,
                                     uint32_t            resolution,
                                     float               splitLambda)
    {
        if (!(camera.nearPlane > 0) || !(camera.farPlane > camera.nearPlane) || !std::isfinite(camera.farPlane) ||
            resolution < 16 || !(casterRadius > 0) || glm::length(directionToLight) < 0.001f || splitLambda < 0 ||
            splitLambda > 1)
        {
            throw std::invalid_argument("Invalid cascade camera, light or settings");
        }
        ShadowCascades           result;
        std::array<glm::vec3, 8> frustum;
        const auto               inverse = glm::inverse(camera.projection * camera.view);
        for (uint32_t corner = 0; corner < 8; ++corner)
        {
            const glm::vec4 clip {corner & 1 ? 1 : -1, corner & 2 ? 1 : -1, corner & 4 ? 1 : 0, 1};
            const auto      world = inverse * clip;
            frustum[corner]       = glm::vec3(world) / world.w;
        }
        const auto      light    = glm::normalize(directionToLight);
        const glm::vec3 up       = std::abs(light.y) < 0.99f ? glm::vec3(0, 1, 0) : glm::vec3(0, 0, 1);
        const auto      view     = glm::lookAtRH(glm::vec3(0), -light, up);
        const auto      caster   = glm::vec3(view * glm::vec4(casterCenter, 1));
        float           previous = camera.nearPlane;
        for (uint32_t cascade = 0; cascade < 4; ++cascade)
        {
            const float fraction    = float(cascade + 1) / 4;
            const float logarithmic = camera.nearPlane * std::pow(camera.farPlane / camera.nearPlane, fraction);
            const float uniform     = camera.nearPlane + (camera.farPlane - camera.nearPlane) * fraction;
            const float split       = glm::mix(uniform, logarithmic, splitLambda);
            result.splits[cascade]  = split;
            // Overlap near edges so the shader can blend into the next cascade.
            const float              start = cascade == 0 ? previous : previous - (previous - camera.nearPlane) * 0.15f;
            const float              t0    = (start - camera.nearPlane) / (camera.farPlane - camera.nearPlane);
            const float              t1    = (split - camera.nearPlane) / (camera.farPlane - camera.nearPlane);
            std::array<glm::vec3, 8> slice;
            glm::vec3                center(0);
            for (uint32_t corner = 0; corner < 4; ++corner)
            {
                slice[corner]     = glm::mix(frustum[corner], frustum[corner + 4], t0);
                slice[corner + 4] = glm::mix(frustum[corner], frustum[corner + 4], t1);
                center += slice[corner] + slice[corner + 4];
            }
            center /= 8;
            float radius = 0;
            for (const auto& corner : slice)
            {
                radius = std::max(radius, glm::length(corner - center));
            }
            radius = std::ceil(radius * 16) / 16;
            radius *= float(resolution) / float(resolution - 2); // one-texel snapping guard band
            auto        lightCenter        = glm::vec3(view * glm::vec4(center, 1));
            const float texel              = 2 * radius / resolution;
            lightCenter.x                  = std::floor(lightCenter.x / texel) * texel;
            lightCenter.y                  = std::floor(lightCenter.y / texel) * texel;
            const float zMin               = std::min(lightCenter.z - radius, caster.z - casterRadius) - 1;
            const float zMax               = std::max(lightCenter.z + radius, caster.z + casterRadius) + 1;
            const auto  projection         = glm::orthoRH_ZO(lightCenter.x - radius,
                                                    lightCenter.x + radius,
                                                    lightCenter.y - radius,
                                                    lightCenter.y + radius,
                                                    -zMax,
                                                    -zMin);
            result.viewProjection[cascade] = projection * view;
            result.worldWidths[cascade]    = radius * 2;
            result.depthRanges[cascade]    = zMax - zMin;
            previous                       = split;
        }
        return result;
    }
} // namespace vultra
