#pragma once

#if defined(__ANDROID__)

struct android_app;

namespace vultra::platform::android
{
    class AndroidDemoAppLauncher
    {
    public:
        static void run(android_app* app, const char* runtimeLibraryName = "libvultra_android_runtime.so");
    };
} // namespace vultra::platform::android

#endif
