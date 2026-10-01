#include "../common/ray_tracing_app.hpp"

#include <vultra/assets/asset_options.hpp>

int runCornellBox(int argc, char** argv)
try
{
    argparse::ArgumentParser cli("example-ray cornell", "0.1.0", argparse::default_arguments::none);
    vultra::addAppOptions(cli);
    vultra::addAssetImportOptions(cli);
    sample::addCaptureOption(cli);
    if (!vultra::parseCommandLine(cli, argc, argv))
    {
        return 0;
    }
    const auto options = sample::getOptions(cli);
    const auto asset =
        vultra::importAsset("resources/models/CornellBox/CornellBox-Original.obj", vultra::getAssetImportOptions(cli));
    sample::RayTracingApp app(options,
                              asset.scene,
                              "examples/ray/cornell_box.slang",
                              true,
                              "Vultra | Ray Tracing - Cornell Box");
    app.run(options.frames);
    vultra::Logger::app().info("Ray tracing Cornell Box: {} frames", app.frameCount());
    return 0;
}
catch (const std::exception& error)
{
    vultra::Logger::app().error("{}", error.what());
    return 1;
}
