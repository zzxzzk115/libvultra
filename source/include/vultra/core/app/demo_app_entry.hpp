#pragma once

#include "vultra/core/app/demo_app_host.hpp"
#include "vultra/core/base/common_context.hpp"

#if defined(__ANDROID__)
#include "vultra/platform/android/android_app_runtime_context.hpp"
#include <android/log.h>
#endif

#include <exception>
#include <type_traits>

#if defined(__ANDROID__)
#define VULTRA_DEMO_APP_MAIN(AppType) \
    extern "C" void vultra_android_run(const vultra::platform::android::AndroidAppRuntimeContext* runtimeContext) \
    try \
    { \
        __android_log_print(ANDROID_LOG_INFO, "VULTRA_CORE", "[DemoAppEntry] Enter vultra_android_run"); \
        static_assert(std::is_base_of_v<::vultra::DemoAppHost, AppType>, \
                      "Android demo entry requires AppType to derive from vultra::DemoAppHost"); \
        AppType app {}; \
        __android_log_print(ANDROID_LOG_INFO, "VULTRA_CORE", "[DemoAppEntry] App constructed"); \
        if (runtimeContext != nullptr) \
        { \
            app.setAndroidRuntimeContext(*runtimeContext); \
            __android_log_print(ANDROID_LOG_INFO, "VULTRA_CORE", "[DemoAppEntry] Runtime context assigned"); \
        } \
        __android_log_print(ANDROID_LOG_INFO, "VULTRA_CORE", "[DemoAppEntry] Calling app.run()"); \
        (void)app.run(); \
        __android_log_print(ANDROID_LOG_INFO, "VULTRA_CORE", "[DemoAppEntry] app.run() returned"); \
    } \
    catch (const std::exception& e) \
    { \
        __android_log_print(ANDROID_LOG_ERROR, "VULTRA_CORE", "[DemoAppEntry] Unhandled exception: %s", e.what()); \
        VULTRA_CORE_ERROR("[DemoAppEntry] Unhandled exception: {}", e.what()); \
    } \
    catch (...) \
    { \
        __android_log_print(ANDROID_LOG_ERROR, "VULTRA_CORE", "[DemoAppEntry] Unknown unhandled exception"); \
    }
#else
#define VULTRA_DEMO_APP_MAIN(AppType) \
    int main(int argc, char** argv) \
    { \
        AppType app {}; \
        return app.run(argc, argv); \
    }
#endif
