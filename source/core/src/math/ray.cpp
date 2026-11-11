#include <vultra/core/math/ray.hpp>

#include <cmath>

namespace vultra
{
    std::optional<float> intersectPlane(const Ray& ray, glm::vec3 point, glm::vec3 normal)
    {
        const float facing = glm::dot(ray.direction, normal);
        if (facing == 0)
        {
            return std::nullopt;
        }
        const float distance = glm::dot(point - ray.origin, normal) / facing;
        if (!std::isfinite(distance) || distance < 0)
        {
            return std::nullopt;
        }
        return distance;
    }

    std::optional<TriangleHit> intersectTriangle(const Ray& ray, glm::vec3 a, glm::vec3 b, glm::vec3 c)
    {
        // Moller-Trumbore: solve origin + t * direction = a + u * (b - a) + v * (c - a).
        const auto  edgeB       = b - a;
        const auto  edgeC       = c - a;
        const auto  p           = glm::cross(ray.direction, edgeC);
        const float determinant = glm::dot(edgeB, p);
        if (determinant == 0)
        {
            return std::nullopt;
        }
        const float inverse = 1 / determinant;
        const auto  offset  = ray.origin - a;
        const float u       = glm::dot(offset, p) * inverse;
        const auto  q       = glm::cross(offset, edgeB);
        const float v       = glm::dot(ray.direction, q) * inverse;
        const float t       = glm::dot(edgeC, q) * inverse;
        if (!std::isfinite(t) || u < 0 || v < 0 || u + v > 1 || t < 0)
        {
            return std::nullopt;
        }
        return TriangleHit {t, {u, v}};
    }

    std::optional<RectangleHit> intersectRectangle(const Ray& ray, glm::vec3 corner, glm::vec3 edgeU, glm::vec3 edgeV)
    {
        const auto distance = intersectPlane(ray, corner, glm::cross(edgeU, edgeV));
        if (!distance)
        {
            return std::nullopt;
        }
        // The edges may be skewed; express the hit in their basis.
        const auto  local = ray.at(*distance) - corner;
        const float uu    = glm::dot(edgeU, edgeU);
        const float uv    = glm::dot(edgeU, edgeV);
        const float vv    = glm::dot(edgeV, edgeV);
        const float scale = uu * vv - uv * uv;
        if (scale == 0)
        {
            return std::nullopt;
        }
        const float lu = glm::dot(local, edgeU);
        const float lv = glm::dot(local, edgeV);
        const glm::vec2 coordinates {(lu * vv - lv * uv) / scale, (lv * uu - lu * uv) / scale};
        if (coordinates.x < 0 || coordinates.x > 1 || coordinates.y < 0 || coordinates.y > 1)
        {
            return std::nullopt;
        }
        return RectangleHit {*distance, coordinates};
    }

    Ray cameraRay(const RenderCamera& camera, glm::vec2 ndc)
    {
        const auto inverse = glm::inverse(camera.projection * camera.view);
        const auto unproject = [&](float depth)
        {
            const auto point = inverse * glm::vec4(ndc, depth, 1);
            return glm::vec3(point) / point.w;
        };
        const auto origin = unproject(0);
        return {origin, glm::normalize(unproject(1) - origin)};
    }
} // namespace vultra
