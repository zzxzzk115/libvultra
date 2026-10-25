#pragma once

#if defined(__ANDROID__)
// NOLINTNEXTLINE(readability-identifier-naming): The borrowed type is defined by GameActivity's C glue.
struct android_app;
#elif defined(__EMSCRIPTEN__)
#include <string>
#endif

namespace vultra
{
    // Borrowed platform entry state, passed explicitly from the executable to its application.
    struct AppLaunch
    {
#if defined(__ANDROID__)
        android_app* activity = nullptr;
#elif defined(__EMSCRIPTEN__)
        std::string canvas = "#canvas";
#endif
    };
} // namespace vultra
