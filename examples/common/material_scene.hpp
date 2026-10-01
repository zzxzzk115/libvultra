#pragma once

#include <vultra/servers/rendering/scene.hpp>

#include <cmath>
#include <numbers>

namespace sample
{
    inline vultra::SceneData makeMaterialScene()
    {
        vultra::SceneData       scene;
        vultra::SurfaceMaterial ground;
        ground.baseColor         = {0.45f, 0.45f, 0.45f, 1};
        ground.specularRoughness = 0.65f;
        scene.materials.push_back(ground);
        scene.vertices = {{{-8, 0, -6}, {0, 1, 0}, {0, 0}},
                          {{8, 0, -6}, {0, 1, 0}, {1, 0}},
                          {{8, 0, 6}, {0, 1, 0}, {1, 1}},
                          {{-8, 0, 6}, {0, 1, 0}, {0, 1}}};
        scene.indices  = {0, 2, 1, 0, 3, 2};
        scene.primitives.push_back({0, 6, 0});
        for (uint32_t row = 0; row < 2; ++row)
        {
            for (uint32_t column = 0; column < 5; ++column)
            {
                vultra::SurfaceMaterial material;
                material.baseColor = row == 0 ? glm::vec4(0.6f, 0.12f, 0.05f, 1) : glm::vec4(0.95f, 0.64f, 0.28f, 1);
                material.baseMetalness     = float(row);
                material.specularRoughness = 0.07f + float(column) * 0.22f;
                material.coatWeight        = row == 0 && column == 0 ? 1.0f : 0.0f;
                const auto materialIndex   = uint32_t(scene.materials.size());
                scene.materials.push_back(material);
                const auto      baseVertex = uint32_t(scene.vertices.size());
                const auto      firstIndex = uint32_t(scene.indices.size());
                const glm::vec3 center {float(column) * 2.4f - 4.8f, 1.0f, float(row) * 3 - 1.5f};
                for (uint32_t y = 0; y <= 32; ++y)
                {
                    for (uint32_t x = 0; x <= 64; ++x)
                    {
                        const float     theta = float(y) / 32 * std::numbers::pi_v<float>;
                        const float     phi   = float(x) / 64 * 2 * std::numbers::pi_v<float>;
                        const glm::vec3 normal {std::sin(theta) * std::cos(phi),
                                                std::cos(theta),
                                                std::sin(theta) * std::sin(phi)};
                        scene.vertices.push_back({center + normal, normal, {float(x) / 64, float(y) / 32}});
                    }
                }
                for (uint32_t y = 0; y < 32; ++y)
                {
                    for (uint32_t x = 0; x < 64; ++x)
                    {
                        const uint32_t a = baseVertex + y * 65 + x;
                        for (uint32_t index : {a, a + 1, a + 65, a + 1, a + 66, a + 65})
                        {
                            scene.indices.push_back(index);
                        }
                    }
                }
                scene.primitives.push_back({firstIndex, uint32_t(scene.indices.size()) - firstIndex, materialIndex});
            }
        }
        scene.center = {0, 0.5f, 0};
        scene.radius = 10;
        return scene;
    }
} // namespace sample
