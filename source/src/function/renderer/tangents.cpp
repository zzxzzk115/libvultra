#include <vultra/function/renderer/scene.hpp>

#include <cmath>
#include <stdexcept>

namespace vultra
{
    void generateTangents(std::span<SceneVertex> vertices, std::span<const uint32_t> indices, uint32_t vertexOffset)
    {
        if (indices.size() % 3 != 0)
        {
            throw std::invalid_argument("Tangents require triangle indices");
        }
        std::vector<glm::vec3> tangents(vertices.size(), glm::vec3(0));
        std::vector<glm::vec3> bitangents(vertices.size(), glm::vec3(0));
        for (size_t first = 0; first < indices.size(); first += 3)
        {
            std::array<uint32_t, 3> corners;
            for (size_t corner = 0; corner < corners.size(); ++corner)
            {
                const auto index = indices[first + corner];
                if (index < vertexOffset || index - vertexOffset >= vertices.size())
                {
                    throw std::invalid_argument("Tangent index exceeds its vertex range");
                }
                corners[corner] = index - vertexOffset;
            }
            const auto& a           = vertices[corners[0]];
            const auto& b           = vertices[corners[1]];
            const auto& c           = vertices[corners[2]];
            const auto  edge1       = b.position - a.position;
            const auto  edge2       = c.position - a.position;
            const auto  uv1         = b.uv - a.uv;
            const auto  uv2         = c.uv - a.uv;
            const float determinant = uv1.x * uv2.y - uv1.y * uv2.x;
            if (std::abs(determinant) <= 1e-12f)
            {
                continue;
            }
            const auto tangent   = (edge1 * uv2.y - edge2 * uv1.y) / determinant;
            const auto bitangent = (edge2 * uv1.x - edge1 * uv2.x) / determinant;
            for (auto corner : corners)
            {
                tangents[corner] += tangent;
                bitangents[corner] += bitangent;
            }
        }
        for (size_t i = 0; i < vertices.size(); ++i)
        {
            auto& vertex = vertices[i];
            if (glm::dot(glm::vec3(vertex.tangent), glm::vec3(vertex.tangent)) > 0)
            {
                continue;
            }
            const auto  normal  = glm::normalize(vertex.normal);
            auto        tangent = tangents[i] - normal * glm::dot(normal, tangents[i]);
            const float length  = glm::length(tangent);
            if (!std::isfinite(length))
            {
                throw std::invalid_argument("Cannot generate a finite tangent frame");
            }
            if (length <= 1e-8f)
            {
                // UV-degenerate vertices still need a finite, orthogonal frame.
                const auto axis = std::abs(normal.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
                tangent         = glm::normalize(axis - normal * glm::dot(normal, axis));
            }
            else
            {
                tangent /= length;
            }
            const float sign = glm::dot(glm::cross(normal, tangent), bitangents[i]) < 0 ? -1.0f : 1.0f;
            vertex.tangent   = glm::vec4(tangent, sign);
        }
    }
} // namespace vultra
