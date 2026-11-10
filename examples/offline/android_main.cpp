#include "offline.hpp"

#include <vultra/core/base/logger.hpp>
#include <vultra/platform/android/asset.hpp>

#include <game-activity/native_app_glue/android_native_app_glue.h>

#include <exception>
#include <string_view>

extern "C" void android_main(android_app* app)
{
    try
    {
        // Keep the APK mapping alive until the graph and all shader entry reads have completed.
        vultra::AndroidAsset mapping(*app->activity->assetManager, "project.vpk");
        const auto           root = std::filesystem::path(app->activity->internalDataPath);
        vultra::VpkArchive   shaders(root / "project.vpk", mapping.bytes());
        OfflineOptions       options {.output = root / "direct"};
        runOfflineExample(options, &shaders);
        const auto definition = shaders.read("color_gain.vgraph");
        auto       graph      = vultra::GraphDefinition::parseExpected(
            std::string_view(reinterpret_cast<const char*>(definition.data()), definition.size()));
        if (!graph)
        {
            vultra::Logger::app().error("Offline graph: {}", graph.error());
            GameActivity_finish(app->activity);
            return;
        }
        options.graph  = std::move(*graph);
        options.output = root / "graph";
        runOfflineExample(options, &shaders);
        vultra::Logger::app().info("OFFLINE_EXAMPLE_PASSED: {}", root.string());
    }
    catch (const std::exception& error)
    {
        vultra::Logger::app().error("Offline example: {}", error.what());
    }
    GameActivity_finish(app->activity);
}
