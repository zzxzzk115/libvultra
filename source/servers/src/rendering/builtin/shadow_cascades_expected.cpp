#include <vultra/servers/rendering/builtin/shadow_cascades.hpp>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace vultra
{
    namespace
    {
        bool finite(glm::vec3 value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        bool finite(glm::vec4 value)
        {
            return finite(glm::vec3(value)) && std::isfinite(value.w);
        }

        bool finite(const glm::mat4& value)
        {
            return finite(value[0]) && finite(value[1]) && finite(value[2]) && finite(value[3]);
        }
    } // namespace

    std::expected<ShadowCascades, std::string> calculateCascadesExpected(const RenderCamera&           camera,
                                                                         glm::vec3                     directionToLight,
                                                                         glm::vec3                     casterCenter,
                                                                         float                         casterRadius,
                                                                         uint32_t                      resolution,
                                                                         float                         splitLambda,
                                                                         std::span<const RenderCamera> additionalViews)
    {
        const float lightLength = glm::length(directionToLight);
        if (!(camera.nearPlane > 0) || !std::isfinite(camera.nearPlane) || !(camera.farPlane > camera.nearPlane) ||
            !std::isfinite(camera.farPlane) || resolution < 16 || !(casterRadius > 0) || !std::isfinite(casterRadius) ||
            !finite(casterCenter) || !finite(directionToLight) || !std::isfinite(lightLength) || lightLength < 0.001f ||
            !std::isfinite(splitLambda) || splitLambda < 0 || splitLambda > 1)
        {
            return std::unexpected("Invalid cascade camera, light or settings");
        }
        std::vector<std::array<glm::vec3, 8>> frusta;
        frusta.reserve(additionalViews.size() + 1);
        const auto appendFrustum = [&](const RenderCamera& current) -> std::expected<void, std::string>
        {
            if (current.nearPlane != camera.nearPlane || current.farPlane != camera.farPlane)
            {
                return std::unexpected("Shared shadow views require common clip planes");
            }
            const auto  combined    = current.projection * current.view;
            const float determinant = glm::determinant(combined);
            if (!finite(current.view) || !finite(current.projection) || !std::isfinite(determinant) || determinant == 0)
            {
                return std::unexpected("Shadow camera matrix is singular or non-finite");
            }
            const auto inverse = glm::inverse(combined);
            if (!finite(inverse))
            {
                return std::unexpected("Shadow camera inverse is non-finite");
            }
            std::array<glm::vec3, 8> frustum;
            for (uint32_t corner = 0; corner < 8; ++corner)
            {
                const glm::vec4 clip {corner & 1 ? 1 : -1, corner & 2 ? 1 : -1, corner & 4 ? 1 : 0, 1};
                const auto      world = inverse * clip;
                if (!finite(world) || std::abs(world.w) < 1e-8f)
                {
                    return std::unexpected("Shadow camera has an invalid frustum corner");
                }
                frustum[corner] = glm::vec3(world) / world.w;
                if (!finite(frustum[corner]))
                {
                    return std::unexpected("Shadow camera has an invalid frustum corner");
                }
            }
            for (uint32_t corner = 0; corner < 4; ++corner)
            {
                const float nearDepth = -(camera.view * glm::vec4(frustum[corner], 1)).z;
                const float farDepth  = -(camera.view * glm::vec4(frustum[corner + 4], 1)).z;
                if (!std::isfinite(nearDepth) || !std::isfinite(farDepth) || !(farDepth - nearDepth > 1e-6f))
                {
                    return std::unexpected("Shadow view rays must advance through reference depth planes");
                }
            }
            frusta.push_back(frustum);
            return {};
        };
        if (auto added = appendFrustum(camera); !added)
        {
            return std::unexpected(added.error());
        }
        for (const auto& view : additionalViews)
        {
            if (auto added = appendFrustum(view); !added)
            {
                return std::unexpected(added.error());
            }
        }
        const auto      light       = directionToLight / lightLength;
        const glm::vec3 up          = std::abs(light.y) < 0.99f ? glm::vec3(0, 1, 0) : glm::vec3(0, 0, 1);
        const auto      view        = glm::lookAtRH(glm::vec3(0), -light, up);
        const auto      caster      = glm::vec3(view * glm::vec4(casterCenter, 1));
        const auto      sliceCorner = [&](const auto& frustum, uint32_t corner, float depth)
        {
            const float nearDepth = -(camera.view * glm::vec4(frustum[corner], 1)).z;
            const float farDepth  = -(camera.view * glm::vec4(frustum[corner + 4], 1)).z;
            const float weight    = std::clamp((depth - nearDepth) / (farDepth - nearDepth), 0.0f, 1.0f);
            return glm::mix(frustum[corner], frustum[corner + 4], weight);
        };
        ShadowCascades result {};
        float          previous = camera.nearPlane;
        for (uint32_t cascade = 0; cascade < 4; ++cascade)
        {
            const float fraction    = float(cascade + 1) / 4;
            const float logarithmic = camera.nearPlane * std::pow(camera.farPlane / camera.nearPlane, fraction);
            const float uniform     = camera.nearPlane + (camera.farPlane - camera.nearPlane) * fraction;
            const float split       = glm::mix(uniform, logarithmic, splitLambda);
            const float start       = cascade == 0 ? previous : previous - (previous - camera.nearPlane) * 0.15f;
            if (!std::isfinite(split) || !std::isfinite(start) || !(split > previous))
            {
                return std::unexpected("Shadow cascade split is non-finite or degenerate");
            }
            result.splits[cascade] = split;
            glm::vec3 center(0);
            for (const auto& frustum : frusta)
            {
                for (uint32_t corner = 0; corner < 4; ++corner)
                {
                    center += sliceCorner(frustum, corner, start) + sliceCorner(frustum, corner, split);
                }
            }
            center /= float(frusta.size()) * 8;
            float radius = 0;
            for (const auto& frustum : frusta)
            {
                for (uint32_t corner = 0; corner < 4; ++corner)
                {
                    for (const float depth : {start, split})
                    {
                        radius = std::max(radius, glm::length(sliceCorner(frustum, corner, depth) - center));
                    }
                }
            }
            radius = std::ceil(radius * 16) / 16;
            radius *= float(resolution) / float(resolution - 2);
            if (!finite(center) || !std::isfinite(radius) || !(radius > 0))
            {
                return std::unexpected("Shadow cascade bounds are non-finite or degenerate");
            }
            auto        lightCenter = glm::vec3(view * glm::vec4(center, 1));
            const float texel       = 2 * radius / float(resolution);
            lightCenter.x           = std::floor(lightCenter.x / texel) * texel;
            lightCenter.y           = std::floor(lightCenter.y / texel) * texel;
            // Quantize outwards so small motion along the light does not continuously invalidate
            // the depth projection, while retaining the complete receiver and caster bounds.
            const float zMin =
                std::floor((std::min(lightCenter.z - radius, caster.z - casterRadius) - 1) / texel) * texel;
            const float zMax =
                std::ceil((std::max(lightCenter.z + radius, caster.z + casterRadius) + 1) / texel) * texel;
            if (!finite(lightCenter) || !std::isfinite(zMin) || !std::isfinite(zMax) || !(zMax > zMin))
            {
                return std::unexpected("Shadow cascade light-space bounds are invalid");
            }
            const auto projection          = glm::orthoRH_ZO(lightCenter.x - radius,
                                                    lightCenter.x + radius,
                                                    lightCenter.y - radius,
                                                    lightCenter.y + radius,
                                                    -zMax,
                                                    -zMin);
            result.viewProjection[cascade] = projection * view;
            result.worldWidths[cascade]    = radius * 2;
            result.depthRanges[cascade]    = zMax - zMin;
            if (!finite(result.viewProjection[cascade]) || !std::isfinite(result.worldWidths[cascade]) ||
                !std::isfinite(result.depthRanges[cascade]))
            {
                return std::unexpected("Shadow cascade transform is non-finite");
            }
            previous = split;
        }
        return result;
    }
} // namespace vultra
