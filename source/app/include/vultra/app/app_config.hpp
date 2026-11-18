#pragma once

#include <vultra/core/math/extent.hpp>

#include <vri/vri.h>

#include <optional>
#include <string>
#include <vector>

namespace vultra
{
    struct AppConfig
    {
        std::string title = "Vultra";
        Extent      size {1280, 720};
        bool        validation = true;
        // Tone-mapped output and ImGui use display-encoded values.
        VriFormat swapchainFormat = VriFormat_BGRA8_UNORM;
        // Required VRI features. Device creation fails explicitly when unavailable.
        uint64_t features = 0;
        // VRI features enabled when the GPU has them; read device features for what was granted.
        uint64_t                optionalFeatures = 0;
        std::optional<uint64_t> renderDocFrame; // zero-based; requires an injected RenderDoc session
        // The graphics APIs to try, in order; empty is Device::create's automatic selection.
        std::vector<VriGraphicsAPI> graphicsAPIs;
    };

} // namespace vultra
