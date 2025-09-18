#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <vector>

template<typename T>
concept HasPosition = requires(T v) {
    { v.position } -> std::convertible_to<glm::vec3>;
};

namespace vultra
{
    struct AABB
    {
        glm::vec3 min, max;

        glm::vec3 getExtent() const;
        glm::vec3 getCenter() const;
        float     getRadius() const;

        void merge(const AABB& other);

        AABB transform(const glm::mat4&) const;

        template<HasPosition VertexType>
        static AABB build(const std::vector<VertexType>& vertices)
        {
            float maxFloat = std::numeric_limits<float>().max();
            AABB  aabb     = {.min = glm::vec3(maxFloat), .max = glm::vec3(-maxFloat)};

            for (const auto& vertex : vertices)
            {
                aabb.min.x = std::min(aabb.min.x, vertex.position.x);
                aabb.min.y = std::min(aabb.min.y, vertex.position.y);
                aabb.min.z = std::min(aabb.min.z, vertex.position.z);

                aabb.max.x = std::max(aabb.max.x, vertex.position.x);
                aabb.max.y = std::max(aabb.max.y, vertex.position.y);
                aabb.max.z = std::max(aabb.max.z, vertex.position.z);
            }

            return aabb;
        }

        // clang-format off
        auto operator<=> (const AABB&) const = delete;
        // clang-format on
    };
} // namespace vultra