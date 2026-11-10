#pragma once

#include <vultra/servers/rendering/scene.hpp>

#include <array>

namespace vultra
{
    // Extract inward-facing planes for VRI's [0, 1] depth convention.
    inline std::array<glm::vec4, 6> frustumPlanes(const glm::mat4& matrix)
    {
        const glm::vec4 x {matrix[0][0], matrix[1][0], matrix[2][0], matrix[3][0]};
        const glm::vec4 y {matrix[0][1], matrix[1][1], matrix[2][1], matrix[3][1]};
        const glm::vec4 z {matrix[0][2], matrix[1][2], matrix[2][2], matrix[3][2]};
        const glm::vec4 w {matrix[0][3], matrix[1][3], matrix[2][3], matrix[3][3]};
        std::array      planes {w + x, w - x, w + y, w - y, z, w - z};
        for (auto& plane : planes)
        {
            const float length = glm::length(glm::vec3(plane));
            if (length > 0)
            {
                plane /= length;
            }
        }
        return planes;
    }

    inline bool intersectsFrustum(const PrimitiveBounds& bounds, const std::array<glm::vec4, 6>& planes)
    {
        for (const auto& plane : planes)
        {
            const glm::vec3 normal(plane);
            const auto support = glm::dot(normal, bounds.center) + plane.w + glm::dot(glm::abs(normal), bounds.extent);
            // Keep touching boxes despite float transform/clip-plane roundoff.
            const auto margin = 1e-4f * (1 + glm::dot(glm::abs(normal), glm::abs(bounds.center) + bounds.extent));
            if (support < -margin)
            {
                return false;
            }
        }
        return true;
    }
} // namespace vultra
