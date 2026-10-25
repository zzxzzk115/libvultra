#pragma once

#include <vultra/platform/input/input.hpp>

// NOLINTNEXTLINE(readability-identifier-naming): Match GameActivity's external C type.
struct android_app;

namespace vultra::platform
{
    // Primary touch acts as the pointer; additional contacts do not synthesize extra mouse presses.
    void processAndroidInput(Input& input, android_app& activity);
} // namespace vultra::platform
