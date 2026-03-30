#pragma once

#if defined(__ANDROID__)

struct ANativeWindow;
struct AAssetManager;
struct android_app;

namespace vultra::platform::android
{
    struct AndroidAppRuntimeContext
    {
        android_app*   app {nullptr};
        ANativeWindow* nativeWindow {nullptr};
        AAssetManager* assetManager {nullptr};
        const int*     destroyRequested {nullptr};
        const char*    internalDataPath {nullptr};
        bool           useBundledVPK {false};
    };
} // namespace vultra::platform::android

#endif
