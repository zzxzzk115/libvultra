#include <android/log.h>
#include <dlfcn.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>

#include "../../../../native/android_runtime_context.hpp"

namespace
{
    using VultraAndroidRunFn = void (*)(const VultraAndroidRuntimeContext*);

    constexpr const char* kCoreTag = "VULTRA_CORE";

    void logCoreInfo(const char* message) { __android_log_print(ANDROID_LOG_INFO, kCoreTag, "%s", message); }

    void logCoreError(const char* message) { __android_log_print(ANDROID_LOG_ERROR, kCoreTag, "%s", message); }

    void logCoreInfo(const char* fmt, const void* value) { __android_log_print(ANDROID_LOG_INFO, kCoreTag, fmt, value); }

    void logCoreError(const char* fmt, const char* value)
    {
        __android_log_print(ANDROID_LOG_ERROR, kCoreTag, fmt, value);
    }

    bool waitForNativeWindow(android_app* app)
    {
        while (app->destroyRequested == 0 && app->window == nullptr)
        {
            int                  events = 0;
            android_poll_source* source = nullptr;
            const int result = ALooper_pollOnce(-1, nullptr, &events, reinterpret_cast<void**>(&source));
            if (result < 0)
            {
                continue;
            }

            if (source != nullptr)
            {
                source->process(app, source);
            }
        }

        return app->window != nullptr;
    }
}

extern "C" void android_main(android_app* app)
{
    logCoreInfo("[AndroidHost] Loading libvultra runtime");

    if (!waitForNativeWindow(app))
    {
        logCoreError("[AndroidHost] Failed to obtain native window");
        return;
    }

    logCoreInfo("[AndroidHost] Native window acquired: %p", app->window);

    const VultraAndroidRuntimeContext runtimeContext {
        .nativeWindow     = app->window,
        .destroyRequested = &app->destroyRequested,
    };

    void* handle = dlopen("libvultra_android_runtime.so", RTLD_NOW);
    if (!handle)
    {
        logCoreError("[AndroidHost] Failed to load libvultra_android_runtime.so: %s", dlerror());
        return;
    }

    auto* run = reinterpret_cast<VultraAndroidRunFn>(dlsym(handle, "vultra_android_run"));
    if (!run)
    {
        logCoreError("[AndroidHost] Failed to find vultra_android_run: %s", dlerror());
        dlclose(handle);
        return;
    }

    logCoreInfo("[AndroidHost] Starting libvultra runtime");
    run(&runtimeContext);
    dlclose(handle);
}
