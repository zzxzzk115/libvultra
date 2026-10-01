#pragma once

#include "colored_mesh.hpp"

#include <vultra/servers/rendering/scene.hpp>

#include <algorithm>
#include <limits>
#include <numbers>

namespace sample
{
    struct Lines
    {
        std::vector<sample::ColoredVertex> vertices;
        std::vector<uint32_t>              indices;

        float boundingRadius(glm::vec3 center) const
        {
            float radius = 0;
            for (const auto& vertex : vertices)
            {
                const glm::vec3 position {vertex.position[0], vertex.position[1], vertex.position[2]};
                radius = std::max(radius, glm::length(position - center));
            }
            return radius;
        }

        void line(glm::vec3 a, glm::vec3 b, glm::vec3 color)
        {
            const auto base = uint32_t(vertices.size());
            vertices.push_back({{a.x, a.y, a.z}, {color.r, color.g, color.b}});
            vertices.push_back({{b.x, b.y, b.z}, {color.r, color.g, color.b}});
            indices.push_back(base);
            indices.push_back(base + 1);
        }

        void box(glm::vec3 low, glm::vec3 high, glm::vec3 color)
        {
            glm::vec3 corners[8];
            for (uint32_t i = 0; i < 8; ++i)
            {
                corners[i] = {i & 1 ? high.x : low.x, i & 2 ? high.y : low.y, i & 4 ? high.z : low.z};
            }
            for (uint32_t i = 0; i < 8; ++i)
            {
                for (uint32_t bit : {1u, 2u, 4u})
                {
                    if (!(i & bit))
                    {
                        line(corners[i], corners[i | bit], color);
                    }
                }
            }
        }
    };

    inline Lines makeDebugLines(const vultra::SceneData& scene)
    {
        Lines     lines;
        glm::vec3 low(std::numeric_limits<float>::max());
        glm::vec3 high(std::numeric_limits<float>::lowest());
        for (const auto& vertex : scene.vertices)
        {
            low  = glm::min(low, vertex.position);
            high = glm::max(high, vertex.position);
        }
        lines.box(low, high, {0, 1, 0});
        const float     scale = scene.radius;
        const glm::vec3 origin {scene.center.x, low.y, scene.center.z};
        auto            line = [&](glm::vec3 a, glm::vec3 b, glm::vec3 color)
        {
            lines.line(origin + a * scale, origin + b * scale, color);
        };
        for (int i = -5; i <= 5; ++i)
        {
            line({float(i), 0, -5}, {float(i), 0, 5}, {0.2f, 0.25f, 0.3f});
            line({-5, 0, float(i)}, {5, 0, float(i)}, {0.2f, 0.25f, 0.3f});
        }
        line({0, 0, 0}, {2, 0, 0}, {1, 0.15f, 0.15f});
        line({0, 0, 0}, {0, 2, 0}, {0.15f, 1, 0.15f});
        line({0, 0, 0}, {0, 0, 2}, {0.2f, 0.4f, 1});
        for (int i = 0; i < 64; ++i)
        {
            const float a = float(i) * 2 * std::numbers::pi_v<float> / 64;
            const float b = float(i + 1) * 2 * std::numbers::pi_v<float> / 64;
            line({std::cos(a), 1 + std::sin(a), 0}, {std::cos(b), 1 + std::sin(b), 0}, {0.2f, 1, 1});
            line({0, 1 + std::cos(a), std::sin(a)}, {0, 1 + std::cos(b), std::sin(b)}, {0.2f, 1, 1});
            line({std::cos(a), 1, std::sin(a)}, {std::cos(b), 1, std::sin(b)}, {0.2f, 1, 1});
        }

        return lines;
    }
} // namespace sample
