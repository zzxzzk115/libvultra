#pragma once

#include <vultra/core/base/error.hpp>
#include <vultra/platform/os/window.hpp>

#include <vri/ext/vri_ext_swapchain.h>

#include <expected>

namespace vultra::platform
{
    VriWindowHandle nativeWindow(const Window& window);
    // Returns nullptr for automatic selection, or "x11"/"wayland". Invalid environment values are errors.
    [[nodiscard]] std::expected<const char*, Error> requestedWindowSystem();
} // namespace vultra::platform
