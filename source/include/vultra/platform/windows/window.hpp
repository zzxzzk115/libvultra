#pragma once

#include <vultra/core/os/window.hpp>

#include <vri/ext/vri_ext_swapchain.h>

namespace vultra::platform
{
    VriWindowHandle nativeWindow(const Window& window);
} // namespace vultra::platform
