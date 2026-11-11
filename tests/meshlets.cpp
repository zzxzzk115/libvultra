#include <vultra/servers/rendering/scene.hpp>

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    vultra::SceneData makeScene()
    {
        vultra::SceneData scene;
        scene.materials.resize(2);
        scene.materials[0].baseColor = {0.8f, 0.2f, 0.1f, 1};
        scene.materials[1].baseColor = {0.1f, 0.3f, 0.8f, 1};
        scene.images = {{2, 2, {200, 128, 240, 255, 150, 180, 240, 255, 180, 100, 240, 255, 128, 128, 255, 255}}};
        scene.materials[0].normalTexture.image = 0;
        scene.materials[1].normalTexture.image = 0;
        for (uint32_t material = 0; material < 2; ++material)
        {
            const auto first = uint32_t(scene.indices.size());
            for (uint32_t y = 0; y < 24; ++y)
            {
                for (uint32_t x = 0; x < 36; ++x)
                {
                    const auto  base = uint32_t(scene.vertices.size());
                    const float px   = (float(x) - 18) * 0.2f;
                    const float py   = (float(y) - 12) * 0.2f;
                    const float z    = material == 0 ? 0 : -0.5f;
                    scene.vertices.insert(scene.vertices.end(),
                                          {{{px, py, z}, {0, 0, 1}, {0, 0}, {1, 1, 1, 1}, {1, 0, 0, 1}},
                                           {{px + 0.18f, py, z}, {0, 0, 1}, {1, 0}, {1, 1, 1, 1}, {1, 0, 0, 1}},
                                           {{px, py + 0.18f, z}, {0, 0, 1}, {0, 1}, {1, 1, 1, 1}, {1, 0, 0, 1}}});
                    scene.indices.insert(scene.indices.end(), {base, base + 1, base + 2});
                }
            }
            scene.primitives.push_back({first, uint32_t(scene.indices.size()) - first, material});
        }
        scene.radius = 5;
        return scene;
    }

    using Triangle = std::array<uint32_t, 3>;

    Triangle canonical(Triangle triangle)
    {
        std::rotate(triangle.begin(), std::min_element(triangle.begin(), triangle.end()), triangle.end());
        return triangle;
    }

    void verifyClusters(const vultra::SceneData& scene)
    {
        const auto parallel = vultra::buildMeshlets(scene, 4);
        const auto serial   = vultra::buildMeshlets(scene, 1);
        require(parallel.vertices == serial.vertices && parallel.triangles == serial.triangles &&
                    parallel.meshlets.size() == serial.meshlets.size(),
                "Meshlet generation depends on worker order");
        for (size_t i = 0; i < parallel.meshlets.size(); ++i)
        {
            require(parallel.meshlets[i].geometry == serial.meshlets[i].geometry &&
                        parallel.meshlets[i].bounds == serial.meshlets[i].bounds,
                    "Meshlet bounds/order changed");
        }
        require(parallel.primitives.size() == scene.primitives.size(), "Meshlet material ranges were lost");
        for (size_t p = 0; p < scene.primitives.size(); ++p)
        {
            const auto range = parallel.primitives[p];
            require(range.count > 32 && range.count % 32 != 0, "Fixture must exercise a partial final task group");
            require(range.first == serial.primitives[p].first && range.count == serial.primitives[p].count,
                    "Meshlet primitive ranges depend on worker order");
            std::vector<Triangle> expected;
            std::vector<Triangle> actual;
            const auto            primitive = scene.primitives[p];
            for (uint32_t i = primitive.firstIndex; i < primitive.firstIndex + primitive.indexCount; i += 3)
            {
                expected.push_back(canonical({scene.indices[i], scene.indices[i + 1], scene.indices[i + 2]}));
            }
            for (uint32_t i = range.first; i < range.first + range.count; ++i)
            {
                const auto& meshlet = parallel.meshlets[i];
                const auto  g       = meshlet.geometry;
                require(g.z <= vultra::kMeshletVertices && g.w <= vultra::kMeshletTriangles,
                        "Meshlet exceeds shader output limits");
                for (uint32_t v = 0; v < g.z; ++v)
                {
                    const auto position = scene.vertices.at(parallel.vertices.at(g.x + v)).position;
                    require(glm::length(position - glm::vec3(meshlet.bounds)) <= meshlet.bounds.w + 0.0001f,
                            "Meshlet sphere excludes a vertex");
                }
                for (uint32_t t = 0; t < g.w; ++t)
                {
                    const auto packed = parallel.triangles.at(g.y + t);
                    Triangle   triangle;
                    for (uint32_t corner = 0; corner < 3; ++corner)
                    {
                        const uint32_t local = (packed >> (corner * 8)) & 255;
                        require(local < g.z, "Invalid meshlet local triangle index");
                        triangle[corner] = parallel.vertices.at(g.x + local);
                    }
                    actual.push_back(canonical(triangle));
                }
            }
            std::sort(actual.begin(), actual.end());
            std::sort(expected.begin(), expected.end());
            require(actual == expected, "Meshlets changed triangle coverage, winding or material assignment");
        }
    }

    void verifyUpload(const vultra::SceneData& scene)
    {
        vultra::Device device(true, nullptr, VriFeature_MeshShader);
        {
            vultra::SceneData emptyScene;
            emptyScene.materials.emplace_back();
            vultra::GpuScene emptyGpu(device, emptyScene, true);
            require(emptyGpu.meshlets && emptyGpu.meshlets->count == 0,
                    "Empty meshlet scene did not retain valid bindings");
        }
        const auto       expected = vultra::buildMeshlets(scene, 4);
        vultra::GpuScene gpu(device, scene, true, 4);
        require(gpu.meshlets && gpu.meshlets->count == expected.meshlets.size(), "GPU meshlet count differs");
        require(std::ranges::none_of(gpu.meshlets->views,
                                     [](const VriDescriptor* view)
                                     {
                                         return view == nullptr;
                                     }),
                "GPU meshlet buffers are missing a shader view");
        require(gpu.meshlets->primitives.size() == expected.primitives.size(), "GPU meshlet ranges were lost");
        for (size_t p = 0; p < expected.primitives.size(); ++p)
        {
            require(gpu.meshlets->primitives[p].first == expected.primitives[p].first &&
                        gpu.meshlets->primitives[p].count == expected.primitives[p].count,
                    "GPU meshlet primitive ranges differ from the built data");
        }
    }
} // namespace

int main()
try
{
    const auto scene = makeScene();
    verifyClusters(scene);
    verifyUpload(scene);
    std::cout << "Meshlet tests passed: triangle/material preservation, bounds, vtask determinism, partial task "
                 "groups, GPU upload ranges\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
