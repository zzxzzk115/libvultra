#include "../common/scene_viewer.hpp"

int runSponza(int argc, char** argv)
try
{
    argparse::ArgumentParser cli("example-scene sponza", "0.1.0", argparse::default_arguments::none);
    cli.add_description("Sponza: original dev assets, OpenPBR / IBL / CSM / PCF / PCSS");
    addSceneViewerOptions(cli,
                          "resources/models/Sponza/Sponza.gltf",
                          "resources/textures/environment_maps/citrus_orchard_puresky_1k.hdr");
    if (!vultra::parseCommandLine(cli, argc, argv))
    {
        return 0;
    }
    SceneViewer app(cli, "Vultra | Sponza", true);
    app.run(cli.present<uint64_t>("--frames").value_or(0));
    vultra::Logger::app().info("Built-in renderer: {} frames", app.frameCount());
    return 0;
}
catch (const std::exception& error)
{
    vultra::Logger::app().error("{}", error.what());
    return 1;
}
