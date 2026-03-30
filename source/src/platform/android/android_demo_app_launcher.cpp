#include "vultra/platform/android/android_demo_app_launcher.hpp"

#if defined(__ANDROID__)

#include "vultra/platform/android/android_app_runtime_context.hpp"

#include <android/asset_manager.h>
#include <android/log.h>
#include <dlfcn.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>

#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <vector>

namespace vultra::platform::android
{
    namespace
    {
        using VultraAndroidRunFn = void (*)(const AndroidAppRuntimeContext*);

        constexpr const char* kCoreTag         = "VULTRA_CORE";
        constexpr const char* kBundledVpkAsset = "resources.vpk";

        void logInfo(const char* message) { __android_log_print(ANDROID_LOG_INFO, kCoreTag, "%s", message); }

        void logError(const char* message) { __android_log_print(ANDROID_LOG_ERROR, kCoreTag, "%s", message); }

        void logInfo(const char* fmt, const void* value)
        {
            __android_log_print(ANDROID_LOG_INFO, kCoreTag, fmt, value);
        }

        void logError(const char* fmt, const char* value)
        {
            __android_log_print(ANDROID_LOG_ERROR, kCoreTag, fmt, value);
        }

        const char* findBundledAssetPath(AAssetManager* assetManager, std::initializer_list<const char*> candidates)
        {
            if (assetManager == nullptr)
                return nullptr;

            for (const char* candidate : candidates)
            {
                if (candidate == nullptr || candidate[0] == '\0')
                    continue;

                AAsset* asset = AAssetManager_open(assetManager, candidate, AASSET_MODE_STREAMING);
                if (asset == nullptr)
                    continue;

                AAsset_close(asset);
                return candidate;
            }

            return nullptr;
        }

        bool copyAssetFile(AAssetManager* assetManager, const char* assetPath, const std::filesystem::path& dstPath)
        {
            if (assetManager == nullptr || assetPath == nullptr || assetPath[0] == '\0')
                return false;

            AAsset* asset = AAssetManager_open(assetManager, assetPath, AASSET_MODE_STREAMING);
            if (asset == nullptr)
                return false;

            std::filesystem::create_directories(dstPath.parent_path());

            std::ofstream out(dstPath, std::ios::binary);
            if (!out)
            {
                AAsset_close(asset);
                return false;
            }

            std::vector<char> buffer(16 * 1024);
            while (true)
            {
                const int32_t bytesRead = AAsset_read(asset, buffer.data(), buffer.size());
                if (bytesRead < 0)
                {
                    AAsset_close(asset);
                    return false;
                }
                if (bytesRead == 0)
                    break;

                out.write(buffer.data(), bytesRead);
                if (!out)
                {
                    AAsset_close(asset);
                    return false;
                }
            }

            AAsset_close(asset);
            return true;
        }

        bool ensureAndroidResourcesExtracted(const AndroidAppRuntimeContext& runtimeContext)
        {
            if (runtimeContext.assetManager == nullptr || runtimeContext.internalDataPath == nullptr ||
                runtimeContext.internalDataPath[0] == '\0')
            {
                return false;
            }

            const std::filesystem::path targetRoot = std::filesystem::path(runtimeContext.internalDataPath);
            const char*                 bundledVpkAssetPath =
                findBundledAssetPath(runtimeContext.assetManager, {kBundledVpkAsset, "resources/resources.vpk"});

            if (bundledVpkAssetPath == nullptr)
            {
                logError("[AndroidDemoAppLauncher] Missing bundled resources.vpk");
                return false;
            }

            std::filesystem::create_directories(targetRoot);

            logInfo("[AndroidDemoAppLauncher] Extracting bundled resources.vpk to internal storage");
            if (!copyAssetFile(runtimeContext.assetManager, bundledVpkAssetPath, targetRoot / kBundledVpkAsset))
                return false;

            return std::filesystem::exists(targetRoot / kBundledVpkAsset);
        }

    } // namespace

    void AndroidDemoAppLauncher::run(android_app* app, const char* runtimeLibraryName)
    {
        if (app == nullptr)
        {
            logError("[AndroidDemoAppLauncher] android_app is null");
            return;
        }

        logInfo("[AndroidDemoAppLauncher] Loading libvultra runtime");

        const AndroidAppRuntimeContext runtimeContext {
            .app              = app,
            .nativeWindow     = app->window,
            .assetManager     = app->activity != nullptr ? app->activity->assetManager : nullptr,
            .destroyRequested = &app->destroyRequested,
            .internalDataPath = app->activity != nullptr ? app->activity->internalDataPath : nullptr,
            .useBundledVPK    = findBundledAssetPath(app->activity != nullptr ? app->activity->assetManager : nullptr,
                                                  {kBundledVpkAsset, "resources/resources.vpk"}) != nullptr,
        };

        const bool extracted = ensureAndroidResourcesExtracted(runtimeContext);
        if (runtimeContext.useBundledVPK && !extracted)
        {
            logError("[AndroidDemoAppLauncher] Bundled resources.vpk was not extracted");
        }

        const char* resolvedLibraryName = (runtimeLibraryName != nullptr && runtimeLibraryName[0] != '\0') ?
                                              runtimeLibraryName :
                                              "libvultra_android_runtime.so";

        void* handle = dlopen(resolvedLibraryName, RTLD_NOW);
        if (!handle)
        {
            logError("[AndroidDemoAppLauncher] Failed to load runtime library: %s", dlerror());
            return;
        }

        auto* run = reinterpret_cast<VultraAndroidRunFn>(dlsym(handle, "vultra_android_run"));
        if (!run)
        {
            logError("[AndroidDemoAppLauncher] Failed to find vultra_android_run: %s", dlerror());
            dlclose(handle);
            return;
        }

        logInfo("[AndroidDemoAppLauncher] Starting libvultra runtime");
        run(&runtimeContext);
        dlclose(handle);
    }
} // namespace vultra::platform::android

#endif
