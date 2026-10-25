#include <vultra/platform/android/asset.hpp>

#include <android/log.h>
#include <android_native_app_glue.h>

#include <exception>
#include <filesystem>

void renderPlatformProbe(std::span<const std::byte> package, const std::filesystem::path& capture);

extern "C" void android_main(android_app* app)
{
    try
    {
        vultra::AndroidAsset package(*app->activity->assetManager, "project.vpk");
        const auto           capture = std::filesystem::path(app->activity->internalDataPath) / "platform.png";
        renderPlatformProbe(package.bytes(), capture);
        __android_log_print(ANDROID_LOG_INFO, "Vultra", "PLATFORM_RENDER_PASSED: %s", capture.c_str());
    }
    catch (const std::exception& error)
    {
        __android_log_print(ANDROID_LOG_ERROR, "Vultra", "Platform render: %s", error.what());
    }
    ANativeActivity_finish(app->activity);
}
