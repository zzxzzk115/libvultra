#pragma once

#include <android/native_window.h>

struct VultraAndroidRuntimeContext
{
    ANativeWindow* nativeWindow {nullptr};
    const int*     destroyRequested {nullptr};
};
