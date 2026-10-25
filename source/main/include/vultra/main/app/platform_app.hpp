#pragma once

#if defined(__ANDROID__)
#include <vultra/main/app/android_app.hpp>
#elif defined(__EMSCRIPTEN__)
#include <vultra/main/app/web_app.hpp>
#else
#include <vultra/main/app/desktop_app.hpp>
#endif

namespace vultra
{
#if defined(__ANDROID__)
    using PlatformApp = AndroidApp;
#elif defined(__EMSCRIPTEN__)
    using PlatformApp = WebApp;
#else
    using PlatformApp = DesktopApp;
#endif
} // namespace vultra
