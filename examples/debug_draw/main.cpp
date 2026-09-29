#include "../common/scene_viewer.hpp"

int main(int argc, char** argv)
try
{
    argparse::ArgumentParser cli("example-debugdraw", "0.1.0", argparse::default_arguments::none);
    cli.add_description("Debug Draw: Damaged Helmet with model bounds, grid, axes and sphere wireframes");
    addSceneViewerOptions(cli,
                          "resources/models/DamagedHelmet/DamagedHelmet.glb",
                          "resources/textures/environment_maps/citrus_orchard_puresky_1k.hdr");
    if (!vultra::parseCommandLine(cli, argc, argv))
    {
        return 0;
    }
    SceneViewer app(cli, "Vultra | Debug Draw", false, true);
    app.run(cli.present<uint64_t>("--frames").value_or(0));
    vultra::Logger::app().info("Debug draw: {} frames", app.frameCount());
    return 0;
}
catch (const std::exception& error)
{
    vultra::Logger::app().error("{}", error.what());
    return 1;
}
