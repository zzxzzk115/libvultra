#include <vultra/servers/rendering/builtin/frame_data.hpp>
#include <vultra/servers/rendering/builtin/shadow_cascades.hpp>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>

namespace
{
    bool containsSlice(const vultra::ShadowCascades& cascades,
                       const vultra::RenderCamera&   reference,
                       const vultra::RenderCamera&   view)
    {
        const auto inverse = glm::inverse(view.projection * view.view);
        for (unsigned cascade = 0; cascade < 4; ++cascade)
        {
            const float previous = cascade == 0 ? reference.nearPlane : cascades.splits[cascade - 1];
            const float start    = cascade == 0 ? previous : previous - (previous - reference.nearPlane) * .15f;
            for (unsigned corner = 0; corner < 4; ++corner)
            {
                const glm::vec4 xy {corner & 1 ? 1 : -1, corner & 2 ? 1 : -1, 0, 1};
                auto            nearPoint = inverse * xy;
                auto            farPoint  = inverse * (xy + glm::vec4(0, 0, 1, 0));
                nearPoint /= nearPoint.w;
                farPoint /= farPoint.w;
                const float nearDepth = -(reference.view * nearPoint).z;
                const float farDepth  = -(reference.view * farPoint).z;
                for (const float depth : {start, cascades.splits[cascade]})
                {
                    const float t         = std::clamp((depth - nearDepth) / (farDepth - nearDepth), 0.f, 1.f);
                    const auto  world     = glm::mix(nearPoint, farPoint, t);
                    const auto  projected = cascades.viewProjection[cascade] * world;
                    const auto  clip      = glm::vec3(projected) / projected.w;
                    if (!std::isfinite(clip.x) || !std::isfinite(clip.y) || !std::isfinite(clip.z) ||
                        std::abs(clip.x) > 1.0001f || std::abs(clip.y) > 1.0001f || clip.z < -0.0001f ||
                        clip.z > 1.0001f)
                    {
                        return false;
                    }
                }
            }
        }
        return true;
    }
} // namespace

int main()
{
    using namespace vultra;
    RenderCamera reference {glm::lookAtRH(glm::vec3(0, 1, 0), glm::vec3(0, 1, -1), glm::vec3(0, 1, 0)),
                            glm::perspectiveRH_ZO(glm::radians(90.f), 1.2f, .1f, 40.f),
                            .1f,
                            40.f};
    auto         left      = reference;
    auto         right     = reference;
    left.view              = glm::lookAtRH(glm::vec3(-.032f, 1, 0), glm::vec3(-.14f, 1, -1), glm::vec3(0, 1, 0));
    right.view             = glm::lookAtRH(glm::vec3(.032f, 1, 0), glm::vec3(.14f, 1, -1), glm::vec3(0, 1, 0));
    left.projection[2][0]  = .18f;
    right.projection[2][0] = -.21f;
    const std::array eyes {left, right};
    const glm::vec3  direction {.3f, 1.f, .2f};
    const glm::vec4  worldPoint {2.1f, -.3f, -8.f, 1.f};
    const auto       sharedDepth = glm::dot(shadowDepthPlane(reference.view), worldPoint);
    if (std::abs(sharedDepth + (reference.view * worldPoint).z) > 1e-5f ||
        std::abs(sharedDepth + (left.view * worldPoint).z) < 1e-3f ||
        std::abs(sharedDepth + (right.view * worldPoint).z) < 1e-3f)
    {
        std::fputs("Shared shadow depth plane does not follow the reference view\n", stderr);
        return 1;
    }
    auto shared = calculateCascadesExpected(reference, direction, {0, 0, 0}, 100.f, 1024, .7f, eyes);
    if (!shared || !containsSlice(*shared, reference, reference) || !containsSlice(*shared, reference, left) ||
        !containsSlice(*shared, reference, right) || !(shared->splits.x > reference.nearPlane) ||
        !(shared->splits.y > shared->splits.x) || !(shared->splits.z > shared->splits.y) ||
        std::abs(shared->splits.w - reference.farPlane) > .001f)
    {
        std::fputs("Shared cascades do not contain the reference and asymmetric canted eye slices\n", stderr);
        return 1;
    }
    const auto move           = glm::translate(glm::mat4(1), -glm::normalize(direction) * .0001f);
    auto       movedReference = reference;
    auto       movedEyes      = eyes;
    movedReference.view *= move;
    for (auto& eye : movedEyes)
        eye.view *= move;
    const auto stable = calculateCascadesExpected(movedReference, direction, {0, 0, 0}, 100.f, 1024, .7f, movedEyes);
    if (!stable || !containsSlice(*stable, movedReference, movedReference) ||
        !containsSlice(*stable, movedReference, movedEyes[0]) || !containsSlice(*stable, movedReference, movedEyes[1]))
    {
        std::fputs("Stable depth quantization lost shared cascade coverage\n", stderr);
        return 1;
    }
    for (size_t i = 0; i < 4; ++i)
    {
        if (stable->viewProjection[i] != shared->viewProjection[i])
        {
            std::fputs("Sub-texel movement along the light changed a stable cascade\n", stderr);
            return 1;
        }
    }
    auto       single = calculateCascadesExpected(reference, direction, {0, 0, 0}, 100.f, 1024, .7f);
    const auto legacy = calculateCascades(reference, direction, {0, 0, 0}, 100.f, 1024, .7f);
    if (!single || !containsSlice(*single, reference, reference))
    {
        return 1;
    }
    for (unsigned i = 0; i < 4; ++i)
    {
        if (std::abs(single->splits[i] - legacy.splits[i]) > 1e-4f ||
            std::abs(single->worldWidths[i] - legacy.worldWidths[i]) > .01f)
        {
            std::fputs("Single-view cascade differs from the established implementation\n", stderr);
            return 1;
        }
    }
    auto wrongClip = left;
    wrongClip.farPlane += 1;
    auto singular        = left;
    singular.projection  = glm::mat4(0);
    auto reversed        = left;
    reversed.view        = glm::lookAtRH(glm::vec3(0, 1, 0), glm::vec3(0, 1, 1), glm::vec3(0, 1, 0));
    auto nanCamera       = reference;
    nanCamera.view[0][0] = std::numeric_limits<float>::quiet_NaN();
    const auto nan       = std::numeric_limits<float>::quiet_NaN();
    if (calculateCascadesExpected(reference, {0, 0, 0}, {}, 1, 1024, .7f) ||
        calculateCascadesExpected(reference, direction, {}, nan, 1024, .7f) ||
        calculateCascadesExpected(reference, direction, {}, 1, 1024, nan) ||
        calculateCascadesExpected(reference, direction, {}, 1, 15, .7f) ||
        calculateCascadesExpected(nanCamera, direction, {}, 1, 1024, .7f) ||
        calculateCascadesExpected(reference, direction, {}, 1, 1024, .7f, std::span(&wrongClip, 1)) ||
        calculateCascadesExpected(reference, direction, {}, 1, 1024, .7f, std::span(&singular, 1)) ||
        calculateCascadesExpected(reference, direction, {}, 1, 1024, .7f, std::span(&reversed, 1)))
    {
        std::fputs("Invalid shadow input was accepted\n", stderr);
        return 1;
    }
    std::puts("Expected shared cascades cover asymmetric canted views and reject invalid input");
    return 0;
}
