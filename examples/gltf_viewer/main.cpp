#include "../common/scene_viewer.hpp"

int main(int argc, char** argv)
try
{
    argparse::ArgumentParser cli("example-gltf-viewer", "0.1.0", argparse::default_arguments::none);
    cli.add_description("glTF Viewer: OpenPBR / IBL / cascaded shadows");
    addSceneViewerOptions(cli, "resources/models/DamagedHelmet/DamagedHelmet.glb");
    if (!vultra::parseCommandLine(cli, argc, argv))
    {
        return 0;
    }
    SceneViewer app(cli, "Vultra | glTF Viewer");
    app.run(cli.present<uint64_t>("--frames").value_or(0));
    vultra::Logger::app().info("Built-in renderer: {} frames", app.frameCount());
    return 0;
}
catch (const std::exception& error)
{
    vultra::Logger::app().error("{}", error.what());
    return 1;
}
