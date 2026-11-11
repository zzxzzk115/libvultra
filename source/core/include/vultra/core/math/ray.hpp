#pragma once

#include <vultra/core/math/render_camera.hpp>

#include <optional>

namespace vultra
{
    // Distances are in units of the direction's length; the direction need not be normalized.
    struct Ray
    {
        glm::vec3 origin {0};
        glm::vec3 direction {0, 0, -1};

        glm::vec3 at(float distance) const
        {
            return origin + direction * distance;
        }
    };

    struct TriangleHit
    {
        float     distance = 0;
        glm::vec2 barycentric {0}; // Weights of the second and third vertex.
    };

    struct RectangleHit
    {
        float     distance = 0;
        glm::vec2 uv {0}; // 0..1 along the two edges from the corner.
    };

    // Hits in front of the origin only. Surfaces are two-sided; a ray parallel to the surface misses.
    std::optional<float>        intersectPlane(const Ray& ray, glm::vec3 point, glm::vec3 normal);
    std::optional<TriangleHit>  intersectTriangle(const Ray& ray, glm::vec3 a, glm::vec3 b, glm::vec3 c);
    std::optional<RectangleHit> intersectRectangle(const Ray& ray, glm::vec3 corner, glm::vec3 edgeU, glm::vec3 edgeV);

    // World-space ray through a point of the image. ndc is in [-1, 1] with +Y up; depth follows VRI's 0..1.
    Ray cameraRay(const RenderCamera& camera, glm::vec2 ndc);
} // namespace vultra
