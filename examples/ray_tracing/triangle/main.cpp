#include "../../common/ray_tracing_app.hpp"

int main(int argc, char** argv)
try
{
    const auto options = sample::readOptions(argc, argv);
    if (!options)
    {
        return 0;
    }
    vultra::Scene scene;
    scene.materials.emplace_back();
    scene.vertices   = {{{0, 0.5f, 0}, {0, 0, -1}, {}},
                        {{-0.5f, -0.5f, 0}, {0, 0, -1}, {}},
                        {{0.5f, -0.5f, 0}, {0, 0, -1}, {}}};
    scene.indices    = {0, 1, 2};
    scene.primitives = {{0, 3, 0}};
    sample::RayTracingApp app(*options,
                              scene,
                              "examples/ray_tracing/triangle/triangle.slang",
                              false,
                              "Vultra | Ray Tracing - Triangle");
    app.run(options->frames);
    vultra::Logger::app().info("Ray tracing triangle: {} frames", app.frameCount());
    return 0;
}
catch (const std::exception& error)
{
    vultra::Logger::app().error("{}", error.what());
    return 1;
}
