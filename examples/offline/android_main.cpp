#include "offline.hpp"

#include <vultra/core/base/logger.hpp>
#include <vultra/platform/android/asset.hpp>

#include <game-activity/native_app_glue/android_native_app_glue.h>

#include <string_view>

namespace
{
    std::expected<std::filesystem::path, vultra::Error> runOffline(android_app& app)
    {
        using namespace vultra;
        // Keep the APK mapping alive until the graph and all shader entry reads have completed.
        const auto mapping = AndroidAsset::open(*app.activity->assetManager, "project.vpk");
        if (!mapping)
        {
            return std::unexpected(mapping.error());
        }
        const auto root    = std::filesystem::path(app.activity->internalDataPath);
        const auto shaders = VpkArchive::create(root / "project.vpk", (*mapping)->bytes());
        if (!shaders)
        {
            return std::unexpected(shaders.error());
        }
        OfflineOptions options {.output = root / "direct"};
        if (auto direct = runOfflineExample(options, &*shaders); !direct)
        {
            return std::unexpected(direct.error());
        }
        const auto definition = shaders->read("color_gain.vgraph");
        if (!definition)
        {
            return std::unexpected(definition.error());
        }
        auto graph = GraphDefinition::parse(
            std::string_view(reinterpret_cast<const char*>(definition->data()), definition->size()));
        if (!graph)
        {
            return std::unexpected(graph.error());
        }
        options.graph  = std::move(*graph);
        options.output = root / "graph";
        if (auto built = runOfflineExample(options, &*shaders); !built)
        {
            return std::unexpected(built.error());
        }
        return root;
    }
} // namespace

extern "C" void android_main(android_app* app)
{
    if (const auto root = runOffline(*app))
    {
        vultra::Logger::app().info("OFFLINE_EXAMPLE_PASSED: {}", root->string());
    }
    else
    {
        vultra::Logger::app().error("Offline example: {}", root.error().message);
    }
    GameActivity_finish(app->activity);
}
