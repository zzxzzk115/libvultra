#include "vultra/core/math/aabb.hpp"

namespace vultra
{
    glm::vec3 AABB::getExtent() const { return max - min; }
    glm::vec3 AABB::getCenter() const { return (max + min) * 0.5f; }
    float     AABB::getRadius() const { return glm::length(getExtent() * 0.5f); }

    void AABB::merge(const AABB& other)
    {
        min.x = std::min(min.x, other.min.x);
        min.y = std::min(min.y, other.min.y);
        min.z = std::min(min.z, other.min.z);

        max.x = std::max(max.x, other.max.x);
        max.y = std::max(max.y, other.max.y);
        max.z = std::max(max.z, other.max.z);
    }

    // https://dev.theomader.com/transform-bounding-boxes/
    AABB AABB::transform(const glm::mat4& m) const
    {
        const auto xa = m[0] * min.x;
        const auto xb = m[0] * max.x;

        const auto ya = m[1] * min.y;
        const auto yb = m[1] * max.y;

        const auto za = m[2] * min.z;
        const auto zb = m[2] * max.z;

        return {
            .min = {glm::min(xa, xb) + glm::min(ya, yb) + glm::min(za, zb) + m[3]},
            .max = {glm::max(xa, xb) + glm::max(ya, yb) + glm::max(za, zb) + m[3]},
        };
    }
} // namespace vultra