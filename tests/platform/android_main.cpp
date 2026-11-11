#include <vultra/platform/android/asset.hpp>

#include <android/log.h>
#include <android_native_app_glue.h>

#include <filesystem>

// Probe failures log through the Vultra logger and exit; only a completed probe reports a pass.
void renderPlatformProbe(std::span<const std::byte> package, const std::filesystem::path& capture);

extern "C" void android_main(android_app* app)
{
    const auto package = vultra::AndroidAsset::open(*app->activity->assetManager, "project.vpk");
    if (package)
    {
        const auto capture = std::filesystem::path(app->activity->internalDataPath) / "platform.png";
        renderPlatformProbe((*package)->bytes(), capture);
        __android_log_print(ANDROID_LOG_INFO, "Vultra", "PLATFORM_RENDER_PASSED: %s", capture.c_str());
    }
    else
    {
        __android_log_print(ANDROID_LOG_ERROR, "Vultra", "Platform render: %s", package.error().message.c_str());
    }
    ANativeActivity_finish(app->activity);
}
