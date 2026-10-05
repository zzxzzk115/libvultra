#include <vultra/servers/rendering/builtin/builtin_renderer.hpp>
#include <vultra/servers/rendering/research/capture.hpp>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>

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

    void verifyPalette(vultra::Device& device, vultra::Environment& environment)
    {
        vultra::SceneData scene;
        scene.materials.resize(2);
        for (uint32_t i = 0; i < 2; ++i)
        {
            const float x    = i == 0 ? -1.0f : 1.0f;
            const auto  base = uint32_t(scene.vertices.size());
            scene.vertices.insert(scene.vertices.end(),
                                  {{{x - 0.9f, -0.9f, -1}, {0, 0, 1}, {0, 0}, {1, 1, 1, 1}, {1, 0, 0, 1}},
                                   {{x + 0.9f, -0.9f, -1}, {0, 0, 1}, {1, 0}, {1, 1, 1, 1}, {1, 0, 0, 1}},
                                   {{x, 0.9f, -1}, {0, 0, 1}, {0, 1}, {1, 1, 1, 1}, {1, 0, 0, 1}}});
            scene.indices.insert(scene.indices.end(), {base, base + 1, base + 2});
            scene.primitives.push_back({i * 3, 3, i});
        }
        scene.radius = 3;
        vultra::GpuScene           gpu(device, scene, true, 1);
        const vultra::RenderCamera camera {glm::mat4(1),
                                           glm::orthoRH_ZO(-2.0f, 2.0f, -1.0f, 1.0f, 0.1f, 10.0f),
                                           0.1f,
                                           10};
        // Original dev hashColor(0), shared by the first meshlet in both submeshes.
        const std::array<float, 3> display {95.0f / 255, 243.0f / 255, 110.0f / 255};
        for (const auto format : {VriFormat_RGBA8_UNORM, VriFormat_RGBA16_SFLOAT})
        {
            vultra::BuiltinRenderer renderer(device, gpu, environment, format);
            renderer.settings.path             = vultra::RenderPath::eNaiveForward;
            renderer.settings.meshShading      = true;
            renderer.settings.meshletColors    = true;
            renderer.settings.shadowResolution = 64;
            vultra::RenderGraph graph(device);
            const auto          outputs = renderer.addPasses(graph, {128, 64});
            graph.exportResource(outputs.color);
            graph.compile();
            for (float exposure : {-4.0f, 0.0f, 4.0f})
            {
                renderer.settings.exposure = exposure;
                renderer.prepare(camera, graph, outputs);
                vultra::Frame frame(device);
                auto*         cmd = frame.begin();
                graph.execute(cmd);
                frame.submitAndWait();
                const auto color = vultra::readback(device, graph.getTexture(outputs.color));
                const auto hdr   = vultra::readback(device, graph.getTexture(outputs.hdr));
                for (uint32_t x : {32u, 96u})
                {
                    const size_t offset = (32 * 128 + x) * 4;
                    for (size_t c = 0; c < display.size(); ++c)
                    {
                        const float linear   = std::pow((display[c] + 0.055f) / 1.055f, 2.4f);
                        const float expected = format == VriFormat_RGBA8_UNORM ? display[c] : linear;
                        require(std::abs(color.rgba[offset + c] - expected) < 0.001f,
                                "Meshlet palette differs from dev or varies with exposure/submesh/output format");
                        require(std::abs(hdr.rgba[offset + c] - linear) < 0.001f,
                                "Meshlet palette must remain linear in the HDR attachment");
                    }
                }
                for (size_t c = 0; c < 3; ++c)
                {
                    require(color.rgba[c] == 0, "Meshlet display background must be black");
                }
            }
        }
    }

    void verifyGpu(const vultra::SceneData& scene)
    {
        vultra::Device      device(true, nullptr, VriFeature_MeshShader);
        vultra::Environment environment(device);
        {
            vultra::SceneData emptyScene;
            emptyScene.materials.emplace_back();
            vultra::GpuScene emptyGpu(device, emptyScene, true);
            require(emptyGpu.meshlets && emptyGpu.meshlets->count == 0,
                    "Empty mesh-shading scene did not retain valid bindings");
            vultra::BuiltinRenderer emptyRenderer(device, emptyGpu, environment);
            emptyRenderer.settings.path             = vultra::RenderPath::eNaiveForward;
            emptyRenderer.settings.meshShading      = true;
            emptyRenderer.settings.skybox           = false;
            emptyRenderer.settings.shadowResolution = 64;
            vultra::RenderGraph emptyGraph(device);
            const auto          emptyOutput = emptyRenderer.addPasses(emptyGraph, {32, 32});
            emptyGraph.exportResource(emptyOutput.color);
            emptyGraph.compile();
            const vultra::RenderCamera camera {glm::mat4(1),
                                               glm::perspectiveRH_ZO(glm::radians(65.0f), 1.0f, 0.1f, 10.0f),
                                               0.1f,
                                               10};
            emptyRenderer.prepare(camera, emptyGraph, emptyOutput);
            vultra::Frame frame(device);
            emptyGraph.execute(frame.begin());
            frame.submitAndWait();
            const auto image = vultra::readback(device, emptyGraph.getTexture(emptyOutput.color));
            require(std::ranges::all_of(image.rgba,
                                        [](float value)
                                        {
                                            return std::isfinite(value);
                                        }),
                    "Empty mesh-shading scene produced nonfinite pixels");
        }
        vultra::GpuScene        gpu(device, scene, true, 4);
        vultra::BuiltinRenderer renderer(device, gpu, environment);
        renderer.settings.path             = vultra::RenderPath::eNaiveForward;
        renderer.settings.skybox           = false;
        renderer.settings.shadowResolution = 64;
        vultra::RenderGraph graph(device);
        const auto          outputs = renderer.addPasses(graph, {128, 128});
        graph.exportResource(outputs.color);
        graph.compile();
        auto render = [&](const vultra::RenderCamera& camera)
        {
            renderer.prepare(camera, graph, outputs);
            vultra::Frame frame(device);
            auto*         cmd = frame.begin();
            graph.execute(cmd);
            frame.submitAndWait();
            auto image = vultra::readback(device, graph.getTexture(outputs.hdr));
            for (float value : image.rgba)
            {
                require(std::isfinite(value), "Meshlet image contains a nonfinite value");
            }
            return image;
        };
        float maxError = 0;
        for (const glm::vec3 eye :
             {glm::vec3(0, 0, 2), glm::vec3(3, 1, 1), glm::vec3(-3, -1, 0.2f), glm::vec3(0, 0, 12)})
        {
            const vultra::RenderCamera camera {glm::lookAtRH(eye, glm::vec3(0, 0, -0.25f), glm::vec3(0, 1, 0)),
                                               glm::perspectiveRH_ZO(glm::radians(65.0f), 1.0f, 0.1f, 10.0f),
                                               0.1f,
                                               10};
            for (uint32_t mode : {0u, 2u})
            {
                renderer.settings.debugMode   = mode;
                renderer.settings.meshShading = false;
                const auto reference          = render(camera);
                renderer.settings.meshShading = true;
                for (bool culling : {false, true})
                {
                    renderer.settings.meshletCulling = culling;
                    const auto image                 = render(camera);
                    for (size_t i = 0; i < image.rgba.size(); ++i)
                    {
                        const float error = std::abs(image.rgba[i] - reference.rgba[i]);
                        maxError          = std::max(maxError, error);
                        require(error < 0.003f, "Task/mesh rendering differs from indexed geometry/materials/normals");
                    }
                }
            }
        }
        const vultra::RenderCamera movingCamera {
            glm::lookAtRH(glm::vec3(0, 0, 2), glm::vec3(0, 0, -0.25f), glm::vec3(0, 1, 0)),
            glm::perspectiveRH_ZO(glm::radians(65.0f), 1.0f, 0.1f, 10.0f),
            0.1f,
            10};
        renderer.settings.debugMode   = 0;
        renderer.settings.meshShading = false;
        const auto stationary         = render(movingCamera);
        gpu.setPrimitiveTransforms(0, uint32_t(scene.primitives.size()), glm::translate(glm::mat4(1), {0.7f, 0, 0}));
        const auto movedIndexed          = render(movingCamera);
        renderer.settings.meshShading    = true;
        renderer.settings.meshletCulling = true;
        const auto movedMesh             = render(movingCamera);
        float      movedDifference       = 0;
        for (size_t i = 0; i < movedMesh.rgba.size(); ++i)
        {
            movedDifference = std::max(movedDifference, std::abs(movedIndexed.rgba[i] - stationary.rgba[i]));
            require(std::abs(movedMesh.rgba[i] - movedIndexed.rgba[i]) < 0.003f,
                    "Transformed meshlet rendering differs from indexed geometry");
        }
        require(movedDifference > 0.1f, "Moving mesh primitives did not change the rendered image");
        std::cout << "Meshlet GPU/indexed HDR max error: " << maxError << '\n';
        verifyPalette(device, environment);
    }
} // namespace

int main()
try
{
    const auto scene = makeScene();
    verifyClusters(scene);
    verifyGpu(scene);
    std::cout
        << "Meshlet tests passed: triangle/material preservation, bounds, vtask determinism, partial task groups, "
           "frustum edges, fully culled groups, indexed/task-mesh HDR and normal parity, dev display palette\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
