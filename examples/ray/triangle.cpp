#include "../common/colored_mesh.hpp"
#include "../common/ray_tracing_app.hpp"

int runRayTriangle(int argc, char** argv)
try
{
    const auto options = sample::readOptions(argc, argv);
    if (!options)
    {
        return 0;
    }
    vultra::SceneData scene;
    scene.materials.emplace_back();
    for (const auto& vertex : sample::kTriangleVertices)
    {
        scene.vertices.push_back({{vertex.position[0], vertex.position[1], vertex.position[2]},
                                  {0, 0, 1},
                                  {},
                                  {vertex.color[0], vertex.color[1], vertex.color[2], 1}});
    }
    scene.indices.assign(sample::kTriangleIndices.begin(), sample::kTriangleIndices.end());
    scene.primitives = {{0, 3, 0}};
    sample::RayTracingApp app(*options, scene, "examples/ray/triangle.slang", false, "Vultra | Ray Tracing - Triangle");
    app.run(options->frames);
    vultra::Logger::app().info("Ray tracing triangle: {} frames", app.frameCount());
    return 0;
}
catch (const std::exception& error)
{
    vultra::Logger::app().error("{}", error.what());
    return 1;
}
