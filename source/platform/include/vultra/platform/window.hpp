#pragma once

#include <vultra/platform/os/window.hpp>

#include <vri/ext/vri_ext_swapchain.h>

namespace vultra::platform
{
    VriWindowHandle nativeWindow(const Window& window);
    // Returns nullptr for automatic selection, or "x11"/"wayland". Invalid environment values throw.
    const char* requestedWindowSystem();
} // namespace vultra::platform
